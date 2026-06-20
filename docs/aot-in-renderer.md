# Research: wiring AOT-compiled JS into the live renderer

This is a design study, not an implementation. It answers the question left
open by `docs/aot-js-compiler.md`: *what would it take to make eligible
JavaScript functions run as native code inside the actual browser, instead of
only through the standalone `experiments/aot-js` tool?* Every claim below is
grounded in the current source with `file:line` citations, because the
sandbox model is what decides whether this is possible at all.

The conclusion up front: **in-renderer dispatch of AOT code is mechanically
feasible, and there is a clean, safe first tier (the browser's own
build-time JavaScript). Extending it to untrusted *web* code is also
mechanically feasible but pays for itself only on compute-heavy pages and
expands the trusted computing base in ways that partly undo the security
argument that motivated AOT over a JIT in the first place.** The two tiers
should be treated as separate decisions.

## 1. The constraint that shapes everything: the renderer sandbox

Untrusted HTML/CSS/JS runs in a dedicated `nordstjernen-renderer` process,
forked and `execv`'d from the shell, talking to it over a `socketpair` (fd 3)
with an HTTP/JSON control channel and a shared-memory framebuffer
(`src/rproc_http.c`, `src/renderer_http.c`). Before it opens any page it
installs a seccomp filter and Landlock rules (`src/libnordstjernen.c:475`,
`src/security.c:703`).

The seccomp filter is **default-deny** (`SCMP_ACT_ERRNO(EPERM)`) with a
syscall allow-list (`src/security.c:432-699`). Two facts from that list
decide the whole design:

- **`mmap`, `mprotect`, `memfd_create` are allowed** (`src/security.c:546,
  554, 556`), each with *no argument filter* — so the renderer *can* map a
  file descriptor `PROT_READ | PROT_EXEC`.
- **`execve`/`execveat` are absent** (`grep -c '"execve"' src/security.c` →
  `0`). The renderer **cannot run a compiler**, or any other program.

So the renderer can *execute* native code that arrives as a read-only,
already-compiled file, but it cannot *produce* it. Compilation must happen in
a process that can `execve`: the shell runs Landlock-only (no seccomp) and
already `fork`/`execv`s helpers (`src/gtk/procview.c:521`); the
`nordstjernen-audio` helper is fully unsandboxed. Either is a candidate
"compile broker."

One nuance worth stating plainly: the seccomp filter does **not**
argument-filter `mprotect`, so it does not *kernel-enforce* W^X today — the
"no writable-executable memory" property in `docs/aot-js-compiler.md:28` is a
property of what the engine *does*, not a wall the kernel imposes. Any
in-renderer scheme should preserve the *behaviour* (never hold a page both
writable and executable), and could be hardened later by adding an
`mprotect` argument filter that rejects `PROT_EXEC | PROT_WRITE`.

## 2. Where native dispatch attaches in the engine

Calls funnel through `JS_CallInternal` (`src/quickjs/quickjs.c:17609`). After
it confirms the callee is a bytecode function it extracts the bytecode:

```c
if (unlikely(p->class_id != JS_CLASS_BYTECODE_FUNCTION)) { ... }   // 17678
b = p->u.func.function_bytecode;                                   // 17688
```

Line 17688 is the natural hook: with `b` in hand and `argc`/`argv` available,
a guard can decide whether to run native code or fall through to the
interpreter `restart:` loop at 17743.

The pieces needed all already exist:

- **A place to hang compiled code.** `JSFunctionBytecode`
  (`src/quickjs/quickjs.c:779-815`) has 5 spare bitfield bits noted at line
  793 and is a single GC allocation (`js_mallocz`, line 36320); a trailing
  field (`void *aot_entry; uint32_t call_count;`) is the obvious home. Its
  teardown is `free_function_bytecode` (declared `quickjs.c:1192`) — the one
  place that must also release/deregister any attached entry.
- **A registry.** `JSRuntime` and `JSContext` each carry a `void *user_opaque`
  (`quickjs.c:354, 541`); the browser already points both at its `ns_js`
  wrapper (`src/js.c:15985-15986`). The compiled-code cache (keyed by a hash
  of `b->byte_code_buf[0..byte_code_len)`) lives there.
- **Argument marshalling.** `JS_IsNumber` (`quickjs.h:759`) tests
  `JS_TAG_INT | JS_TAG_FLOAT64`; `JS_VALUE_GET_INT` / `JS_VALUE_GET_FLOAT64`
  extract a `double`; `__JS_NewFloat64` / `js_int32` (`quickjs.c:222, 1519`)
  box the result. This is exactly the value-domain `aotc` already targets.

The dispatch guard is therefore small:

```c
b = p->u.func.function_bytecode;
ns_aot_entry *e = b->aot_entry;            /* NULL unless compiled */
if (e && argc >= b->arg_count && ns_args_all_numeric(argv, b->arg_count)) {
    double a[NS_AOT_MAX_ARGS];
    for (int i = 0; i < b->arg_count; i++) a[i] = ns_to_double(argv[i]);
    return __JS_NewFloat64(e->fn(a));      /* native call; no interp frame */
}
/* else: fall through to the bytecode loop exactly as today */
```

The type guard is the bailout: any non-numeric argument (a string, an object,
`undefined`) skips the native path and interprets, so correctness is never at
risk — identical to how `aotc` *declines* at compile time, now mirrored as a
*runtime* guard. This matches the standalone tool's contract: native output
is only ever taken when it is provably identical to interpreting.

## 3. Three ways to get the native code — and their costs

The hook above is agnostic about *where* `e->fn` comes from. There are three
strategies, and they differ enormously in security posture.

### Tier A — Build-time AOT of the browser's own JavaScript (clean)

The browser ships **5,656 lines of trusted JavaScript** it `eval`s at startup
(`data/js/polyfills.js`, compiled to `polyfills.h`, evaluated at
`src/js.c:35729`). This code is *known at build time* and *trusted*. Running
`aotc` over it during the build, compiling the eligible functions with the
normal toolchain, and linking them into the binary makes them part of the
**read-only executable image** — mapped `r-x` like every other function, no
runtime codegen, no new syscalls, no compiler in the loop. The registry is
then a static table populated at startup by matching polyfill bytecode hashes
to linked symbols.

This is "AOT" in the strict sense and costs essentially nothing in attack
surface. The benefit is bounded by how much of the polyfill layer is numeric
(date math, `TypedArray`/`DataView` helpers, `Math` shims, hashing) — modest,
but free and safe, and it exercises the entire dispatch/guard/marshalling
path end to end. **This is the right first step**: it makes the wiring real
and measurable without touching the threat model.

### Tier B — A compile broker for web code (feasible, heavy)

To accelerate *page* JavaScript, the code is only known at runtime, so
something must compile it while the page runs. Because the renderer cannot
`execve`, the only shape that preserves "no W^X in the renderer" is:

1. The renderer runs the `aotc` **analysis and C-emission** (pure
   computation — allowed) on a hot function's bytecode, producing C source.
2. It sends that C text over the existing fd-3 IPC to the **shell/broker**,
   which `fork`/`execv`s `cc` (it already spawns helpers, `procview.c:521`).
3. The broker compiles to a `.so` and returns the **file descriptor** via
   `SCM_RIGHTS`; the renderer `mmap`s it `PROT_READ | PROT_EXEC` (allowed) and
   registers `e->fn`. The page stays interpreted until the `.so` is ready
   (compilation is tens to hundreds of ms), so this is a background tier, gated
   by a `call_count` hotness threshold on `JSFunctionBytecode`.

It works, and the renderer never holds writable-executable memory. But the
costs are real and must be stated honestly:

- **The trusted computing base now includes a C compiler processing
  attacker-influenced input.** `aotc`'s codegen is proven sound, but `cc`
  itself is enormous; a codegen or compiler bug becomes native-code execution
  in the renderer. A JIT, the thing AOT was chosen *over*, at least keeps a
  small fixed code generator instead of a general-purpose toolchain.
- **The renderer executes native code derived from untrusted content** (even
  if read-only), and a `.so` cache on disk raises poisoning/persistence
  questions and needs Landlock-scoped, hash-named, integrity-checked storage.
- **The benefit on typical pages is near zero.** The Speedometer 3.1 corpus
  measured **0 of 2154** top-level functions as numeric-eligible
  (`docs/aot-js-compiler.md`); web UI is DOM/object/string-bound. The win is
  concentrated in *compute-heavy* pages — games, crypto, codecs, image/audio
  processing, and the framework numeric kernels in `tests/framework.js`
  (React lane math, easing) — not in everyday browsing.

So Tier B buys a small, narrow speedup at the price of the exact property
(no runtime path from untrusted input to executed native code) that made AOT
attractive. For a security-first clean-room browser that is a poor trade, and
the recommendation is **not** to ship it by default — at most an
opt-in/experimental flag, the way WebGPU is gated.

### Tier C — In-process template stitching (a JIT by another name)

A third option avoids the external `cc`: keep a library of pre-built
machine-code templates for each supported op and stitch them in a buffer at
runtime (copy-and-patch / baseline-JIT style). The TCB shrinks back to a
small fixed generator. But stitching requires writing into a buffer and then
making it executable — `mprotect(PROT_EXEC)` on memory that was writable —
i.e. **runtime W^X in the renderer**. The seccomp filter permits the syscalls
today, but this is precisely the behaviour the project's design forbids, and
adopting it would mean abandoning the no-W^X stance. If that line is ever
crossed deliberately, a baseline JIT is a better-understood destination than
a bespoke stitcher; this study notes Tier C only to bound the design space.

## 4. Recommended path and open problems

**Recommend:** implement Tier A end-to-end (build-time AOT of `polyfills.js`),
because it is safe, real, and validates the dispatch machinery; treat Tier B
as research kept behind an explicit experimental flag if compute-heavy pages
ever justify it; do not pursue Tier C without a deliberate reversal of the
no-W^X policy.

Problems any tier must still solve (none blocking, all real):

- **GC lifecycle.** The cache entry must be released in
  `free_function_bytecode` (`quickjs.c:1192`) and survive bytecode that is
  serialized/relocated; keying on a content hash rather than the `b` pointer
  is safer across `JS_ReadObject`.
- **Closures and `var_ref`s.** `aotc` today compiles top-level numeric
  functions; functions that capture mutable outer variables
  (`JSFunctionBytecode.closure_var`, line ~785) are out of scope until the
  ABI carries captured cells. The guard simply never fires for them.
- **Array-parameter marshalling.** Scalar dispatch is a flat `double[]`;
  `Float64Array` parameters (already compiled by `aotc`) need the guard to
  recognise the typed-array class and pass `(ptr, len)` — a second,
  well-defined marshalling path, not a new compiler capability.
- **Hotness and de-opt.** A `call_count` threshold avoids compiling cold
  functions; because the guard re-checks argument types on every call, there
  is no speculative de-opt machinery to build — non-numeric calls just take
  the interpreter, which is the existing behaviour.
- **Cross-platform.** Tier A is portable (it is just linked code). Tier B's
  broker + `mmap(PROT_EXEC)` story is Linux-shaped; macOS needs a signed/JIT
  entitlement and Windows a different mapping path, so Tier B would start
  Linux-only.

## 5. Bottom line

The engine is ready for this: there is a single clean hook
(`quickjs.c:17688`), spare room on `JSFunctionBytecode`, opaque slots for a
registry, and the exact numeric value-marshalling `aotc` already assumes. The
sandbox permits mapping read-only native code from a descriptor while
correctly refusing to let the renderer run a compiler. **The trusted,
build-time tier is a safe, finishable next step that turns the standalone
experiment into a real in-browser capability.** Accelerating untrusted web
code is possible through a compile broker but is a separate, weightier
decision: it helps only compute-heavy pages and trades away much of the
security margin that distinguishes this approach from a JIT — so it belongs
behind an experimental flag, if anywhere, not in the default build.

## 6. Implementation status — the dispatch mechanism is built

The shared foundation both tiers need — the in-engine dispatch path — is now
implemented and verified in the real engine (the same `quickjs.c` the renderer
runs), gated behind the experimental compile switch `NS_AOT_DISPATCH` so the
**stock build is byte-for-byte unchanged** (it carries no AOT code at all,
exactly like the WebGPU pattern).

What landed:

- **Registration API** `JS_SetFunctionAOT(ctx, func, entry)`
  (`src/quickjs/quickjs.c`, gated) stamps a borrowed `JSAOTEntry { JSAOTFn fn;
  int arg_count; }` onto a normal function's `JSFunctionBytecode`.
- **Dispatch hook** at the `JS_CallInternal` site (`quickjs.c`, the
  `b = p->u.func.function_bytecode;` point): if an entry is registered and
  every argument is a number, it marshals the args to `double`, calls the
  native function, and boxes the result with `JS_NewFloat64`; **any
  non-numeric argument, too few arguments, or a constructor/generator call
  falls straight through to the interpreter** — so the result is always
  identical to interpreting.
- **`aotc --registry`** emits a linkable C file (numeric functions + a
  `JSAOTEntry` table + an `ns_aot_register(ctx, obj)` that binds each entry to
  the matching function by name) instead of a standalone `main`.

How it is verified (`experiments/aot-js/dispatchtest.sh`):

- `dispatch_test.c` proves, in the live engine, that the native path is
  actually taken (a deliberately wrong "spy" native return is observed for
  numeric args), that a string argument and an under-application both bail to
  the interpreter, and that a real `easeInOutCubic` kernel matches the
  interpreter bit-for-bit across 101 points.
- `registry_test.c` runs `aotc --registry` over `tests/framework.js`, links
  the output against the engine, registers all 13 kernels, and confirms native
  results equal a second unregistered (interpreter) context across a 705-point
  domain sweep.
- The **full GTK browser builds** with these changes (`meson compile`, 601
  targets) and **runs headless** end-to-end (`--headless --url=… --dump=…`,
  exit 0), and the stock (`NS_AOT_DISPATCH` off) build is unchanged.
- Standalone soundness is unaffected: `selfcheck` 160/160, `arraytest` 26/26.

What is deliberately **not** wired yet, and why: activating Tier A against the
browser's own JavaScript would mean a meson `custom_target` running
`aotc --registry` over `data/js/polyfills.js` and an `ns_aot_register(ctx,
global)` call after the polyfills are evaluated (`src/js.c:35729`). Measured
first: `polyfills.js` is an IIFE of DOM/web shims with **zero numeric
top-level functions** — registering it would bind nothing. So the honest state
is that the *mechanism* is finished and proven in-engine, but there is no
trusted numeric corpus shipping today to point it at; the remaining work is to
either add such a module or take the Tier-B broker decision. Wiring an empty
registration would be dead machinery, so it is left out until there is a real
target.
