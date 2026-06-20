# Ahead-of-time JavaScript compilation for QuickJS

This document describes Nordstjernen's **ahead-of-time (AOT)** JavaScript
compiler: a tool that lowers QuickJS bytecode to native code through C, the
analysis that makes it *sound*, the way it is *enabled by default* with
transparent fallback, and why AOT is a better fit for the sandboxed
renderer than a traditional **just-in-time (JIT)** compiler.

The compiler lives in `experiments/aot-js/`. It is **sound by
construction**: it emits native code for a function only when it can prove
the result is bit-for-bit identical to interpreting it, and otherwise
declines so the caller falls back to the interpreter. It therefore can
never change the meaning of a program — at worst a function is not
accelerated. It currently accelerates the **numeric subset** of
JavaScript; everything else runs interpreted, exactly as before.

## TL;DR

- `aotc` reuses the **real QuickJS front end** to compile JS to bytecode,
  proves a function is in the safe numeric subset, and translates that
  bytecode to C, which the system compiler turns into native code.
- It is **sound**: an *eligibility analysis* (whitelisted opcodes, numeric
  constants only, calls only to other eligible functions, consistent
  operand-stack depth) gates every function. Ineligible code is declined,
  not miscompiled.
- AOT is **enabled by default**: `aot-run` always tries AOT first and
  **falls back to the interpreter automatically** when a program is outside
  the subset (or if the generated C fails to compile). Results are identical
  either way.
- The subset covers arithmetic, comparisons, `if`/`while`/`for`/`do…while`,
  `?:`, short-circuit `&&`/`||`, `++`/`--`, logical/bitwise-not, the full
  **bitwise and shift operators** (`& | ^ << >> >>>`) with spec-exact
  `ToInt32`/`ToUint32`, **`Math.*`** (sqrt, floor, sin, pow, min/max, …, and
  constants like `Math.PI`) lowered to the exact libm calls QuickJS itself
  uses, and direct/mutual/tail recursion plus calls (with correct
  under/over-application) among eligible functions.
- An 89-case self-check (`selfcheck.sh`) compares AOT against the
  interpreter across all of the above — including bit hashing (FNV),
  xorshift PRNG, popcount, `Math.*` kernels, division-by-zero, negative
  modulo, fractional powers, `(±1) ** ±Infinity`, and `ToInt32` edge cases
  — **all identical**; out-of-subset programs (strings, arrays, objects,
  higher-order calls) correctly fall back.
- It is exercised on **real third-party code**: the SunSpider numeric
  kernels (`frameworktest.sh`, 9 native / 17 fallback / **0 mismatches**)
  and the **entire Speedometer 3.1 JavaScript corpus** (`speedometer.sh`,
  424 files / 14 MB) — **0 compiler crashes**. This testing found and fixed
  a real crash and the `pow` soundness bug (see *Security review*).
- It runs **1.8×–36×** faster than the interpreter on numeric workloads
  (6.3× on a `Math.*` kernel); for `fib(28)` it retires **9.2M**
  instructions versus the interpreter's **1.06 billion**. Callgrind
  attributes **93%** of interpreter instructions to the bytecode dispatch
  loop alone.
- The compiler is checked under **ASan + UBSan** across the whole corpus
  (including real-world code and a 2000-local stress program): **0
  memory-safety errors**.
- Crucially, **all codegen happens ahead of time**. The runtime never
  needs writable-executable memory, so the renderer's `seccomp` sandbox can
  keep denying it — unlike every JIT.

## Why AOT and not JIT

Nordstjernen runs untrusted JavaScript inside a `seccomp`-filtered,
unprivileged renderer (see `docs/tab-isolation.md`, `docs/watchdog.md`).
The single most dangerous capability a scripting engine can ask of such a
sandbox is the ability to **create executable memory at runtime**. Every
production JS JIT does precisely that: it allocates RW pages, writes
generated machine code into them, and flips them to RX (or keeps them RWX)
with `mprotect`/`VirtualProtect`/`mmap(MAP_JIT)`. That single allowance:

- punches a hole straight through W^X — an attacker who gains a write
  primitive can stage shellcode in a page that will become executable;
- is the substrate for the entire class of **JIT-spraying** and
  **type-confusion → JIT** exploits that dominate real-world browser CVEs;
- forces the sandbox policy to allow `mprotect(PROT_EXEC)`/`memfd_create`,
  weakening the filter for every other code path too.

AOT moves codegen to **build time**. The machine code is produced by a
trusted, offline C compiler and mapped from a read-only executable image
like any other program text. At runtime the engine never writes code, never
calls `mprotect(PROT_EXEC)`, and the `seccomp` policy keeps denying it. AOT
trades JIT's adaptive, profile-guided specialization for a strictly smaller
attack surface — the right trade for a security-first browser.

This is why the AOT path never compiles at runtime, and why it does **not**
shell out to a compiler from inside the renderer: doing so would reintroduce
exactly the runtime-codegen capability the design exists to avoid. The
generated binaries confirm the property — they import no
`mmap`/`mprotect`/`memfd` symbols, and QuickJS itself contains zero
`PROT_EXEC` call sites:

```
$ objdump -T mandel | grep -iE 'mprotect|mmap|memfd'
  (no executable-memory syscalls)
$ grep -cE 'mprotect|PROT_EXEC' src/quickjs/quickjs.c
0
```

## How QuickJS compiles JavaScript (what the compiler builds on)

The AOT compiler does **not** re-implement a JS parser; it reuses
QuickJS's, which already does the hard work. The pipeline in
`src/quickjs/quickjs.c` is:

1. **Parse + emit (`js_parse_program`, `js_parse_*`).** QuickJS is a
   single-pass compiler: the recursive-descent parser emits stack-machine
   bytecode into a growing buffer as it goes, rather than building a
   persistent AST. Nested functions become constant-pool entries.
2. **`resolve_variables`.** Resolves variable references to concrete local
   / argument / closure-variable slots and selects the specialized access
   opcodes (`get_loc8`, `get_arg0`, `get_var_ref0`, …).
3. **`resolve_labels`.** Resolves symbolic jump labels to relative byte
   offsets, runs a **peephole optimizer**, and substitutes the compact
   *short opcodes* (`push_0`, `get_loc0`, `if_false8`, `goto8`, `tail_call`,
   …) that make the final bytecode dense.
4. **`compute_stack_size`.** Abstract-interprets the bytecode to find the
   maximum operand-stack depth (`stack_size`).
5. **`js_create_function`.** Packages everything into a
   `JSFunctionBytecode` (`quickjs.c:779`): `byte_code_buf`, `cpool`,
   `vardefs`, `closure_var`, counts, and debug tables.

At run time `JS_CallInternal` (`quickjs.c:17609`) interprets that bytecode
with a **computed-goto threaded dispatcher**. Each opcode is a label in a
256-entry jump table; every operand is a tagged `JSValue`, and
reference-counted values are `js_dup`/`JS_FreeValue`d as they move through
the operand stack.

The interpreter pays three taxes on every operation that native code does
not: **dispatch** (an indirect branch per opcode), **boxing** (tag checks
and slow-path fallbacks on tagged `JSValue`s), and **refcount churn**
(`js_dup`/`JS_FreeValue` around stack traffic). Removing those three taxes
for provably-numeric code is the entire speedup.

## How the AOT compiler works

`experiments/aot-js/aotc.c` is a single translation unit that
`#include "quickjs.c"`, giving it the internal `JSFunctionBytecode` layout,
the `OP_*` enum, and the `opcode_info[]` size table. Its flow:

1. **Front end.** `JS_Eval(..., JS_EVAL_FLAG_COMPILE_ONLY)` compiles the
   input with the real QuickJS compiler and returns the top-level function
   bytecode without running it. Nested top-level functions are read out of
   the constant pool.

2. **Eligibility analysis (soundness gate).** A function is *locally OK*
   only if **every** opcode is on the supported whitelist (`op_delta` is
   the single source of truth) and every pushed constant is numeric. Two
   abstract interpretations then run over the bytecode: `compute_sp`
   computes the operand-stack depth at every program point (rejecting any
   inconsistency across a control-flow join), and `validate_kinds` tracks a
   *kind* for every stack slot — number, user-function reference, the `Math`
   object, or a `Math` method — and proves that **no non-number ever flows
   into a numeric context**, that every call targets a statically-known
   function, and that every method call / property access targets a
   whitelisted `Math` member. (This is what makes `Math + 1`, calling a
   function-valued argument, or storing `Math` in a local all decline rather
   than miscompile.) A function is finally **eligible** only if it is locally
   OK *and every function it calls is eligible* — a monotone greatest
   fixpoint over the call graph that handles direct and mutual recursion.
   Anything that fails is declined.

3. **Lowering.** For each eligible function the operand stack is modelled
   as a set of fixed `double` slot variables `s0..sN` indexed by the
   depth computed in step 2. Every opcode reads and writes specific slots:
   `add` becomes `s[sp-2] = s[sp-2] + s[sp-1]`, `if_false` becomes
   `if (!s[sp-1]) goto Lx`, a call becomes `s[f] = callee(args…)`,
   `tail_call` becomes `return callee(args…)`. Because the slot depth is
   path-independent (QuickJS guarantees it), values flow correctly across
   labels — so short-circuit `&&`/`||` and ternaries, which leave a value
   on the stack across a branch, lower correctly. Branch targets use the
   interpreter's own arithmetic: `operand_position + signed_operand`.

4. **Output.** A `.c` file with one `static double` function per eligible
   JS function, plus a timing `main`. The system `cc` compiles it to a
   native binary.

### Why "everything is a `double`" is correct here

JavaScript numbers *are* IEEE-754 doubles, and the whitelisted operators are
defined on them with exactly the semantics C gives `double`: `%` is `fmod`,
division by zero yields `±Inf`/`NaN`, comparisons yield `0`/`1`. The
eligibility gate (see *kind tracking* below) guarantees the only values
flowing through a compiled function are numbers — so the `double` model is
not an approximation, it is the spec.

The bar for adding an operator is **bit-for-bit agreement with the QuickJS
interpreter**, not merely with the ECMAScript spec — soundness means
"identical to interpreting it in *this* engine." That principle drove two
non-obvious choices:

- **Bitwise/shift** (`& | ^ ~ << >> >>>`) apply `ToInt32`/`ToUint32`, which
  the emitter implements exactly (with an in-range fast path), so they match
  the interpreter's integer results.
- **`**` and `Math.pow`** do *not* map to C `pow`. QuickJS returns `NaN` for
  `(±1) ** ±Infinity` where C `pow` returns `1`; the emitter therefore uses a
  `js_pow` helper that reproduces QuickJS's own `js_math_pow`. (Catching this
  fixed a latent unsoundness in the original `**` lowering.)

`Math.*` is sound for the same reason: QuickJS implements `Math.sqrt`,
`Math.floor`, `Math.sin`, … as one-line calls to the C library
(`js_math_sqrt(d){return sqrt(d);}`), so emitting the identical libm call is
bit-identical. The handful of non-libm methods (`min`/`max`/`sign`/`round`)
are reproduced from QuickJS's exact implementations (`js_fmin`/`js_fmax` ±0
handling, the bit-twiddling `js_round`), and `Math.PI` and friends are read
straight off the live `Math` object so the emitted literal is the exact
double the interpreter holds.

### Worked example

Input (`bench/fib.js`):

```js
function fib(n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}
```

Emitted C (operand stack shown as `s0/s1`, recursion resolved through the
closure variable):

```c
static double fib(double a0) {
  double s0 = 0, s1 = 0;
  s0 = a0;
  s1 = 2;
  s0 = (s0 < s1);
  if (!(s0)) goto L7;
  s0 = a0;
  return s0;
L7: ;
  /* fib(n-1) */ s0 = a0; s1 = 1; s0 = s0 - s1; s0 = fib(s0);
  /* fib(n-2) */ s1 = a0; s2 = 2; s1 = s1 - s2; s1 = fib(s1);
  s0 = s0 + s1;
  return s0;
}
```

The tagged stack machine collapses into plain `double` arithmetic and
direct calls; `-O2` then register-allocates the slots away.

## Enabled by default, with automatic fallback

`experiments/aot-js/aot-run.sh` is the entry point and **AOT is on by
default**. For a given program it:

1. runs `aotc`, which either AOT-compiles the entry and its (transitively
   eligible) callees, or exits with a distinct *decline* status;
2. on success, compiles the generated C and runs the **native** binary;
3. on decline, transparently runs the program through the **QuickJS
   interpreter** instead.

The result is identical either way, because AOT only engages when it is
provably equivalent. Out-of-subset programs are never broken — they simply
take the interpreted path, exactly as today. `FORCE_INTERP=1` disables AOT
(used to produce the reference output in testing).

## Soundness: the self-check

`experiments/aot-js/selfcheck.sh` runs every test program **both** ways —
pure interpreter (reference) and AOT-by-default — over many inputs, and
asserts the numeric results are identical *and* that the expected path was
taken (numeric programs go native; out-of-subset programs fall back). It
covers arithmetic, short-circuit `&&`/`||`, ternaries, `for`/`while`/
`do…while`, mutual recursion (`tail_call`), float kernels, logical `!`,
bitwise/shift kernels (FNV hash, xorshift PRNG, popcount), `Math.*` kernels,
division by zero (`±Inf`/`NaN`), negative modulo, fractional powers, and
JS-name/C-keyword collisions (functions called `main`, `pow`, `int`), plus
programs that must fall back (strings, arrays, higher-order calls).

```
passed: 89   failed: 0
```

Every eligible case matches the interpreter bit-for-bit; every ineligible
case falls back and still matches. That is the soundness guarantee made
concrete.

## Testing against real-world code

`experiments/aot-js/frameworktest.sh` runs the compiler against code it did
not author — the SunSpider numeric kernels (`math-cordic`,
`math-spectral-norm`, `bitops-*`, `access-nbody`, `controlflow-recursive`,
…) — which freely mix numeric kernels with arrays, globals, `Date`, and
higher-order calls. For every top-level function it reports native vs
fallback, and verifies each AOT-compiled function against the interpreter:

```
native: 9   fallback: 17   mismatches: 0
```

The pure-numeric kernels compile and match exactly — Ackermann (`ack`),
Takeuchi (`tak`), `fib`, the cordic fixed-point helpers, the bit-counting
loops, and the spectral-norm index function `A(i,j)`. Everything touching
arrays, globals, `Date`, or function-valued arguments falls back. This is
the eligibility analysis and fallback validated on real programs rather than
hand-written tests — and it is how the crash and arity bugs in *Security
review* were found.

### The Speedometer 3.1 corpus

`experiments/aot-js/speedometer.sh` pushes this much harder:
it runs the compiler over **every JavaScript file Speedometer 3.1 ships** —
**424 files, 14 MB**, including minified React/Vue/Angular bundles,
CodeMirror, TipTap, chart.js, and the news-site apps:

```
files processed:        424
compiler crashes:       0
top-level functions:    2154
AOT-eligible (numeric): 0
```

Two findings, both expected and both important:

1. **Robustness:** the compiler survives 14 MB of adversarial, minified,
   real-world code with **zero crashes** — a strong fuzz-like signal on top
   of the sanitizer runs.
2. **Applicability:** **none** of the 2154 top-level functions are
   numeric-eligible. Web-application code is objects, strings, DOM, and
   closures — not numbers — so AOT correctly declines all of it and the
   interpreter runs everything, exactly as it does today.

This is also the honest answer to "will this speed up the Speedometer
score?" — **no**, and it cannot. Speedometer's code is web content loaded at
*runtime*; compiling it would require runtime codegen (a JIT), which needs
the writable-executable memory this whole approach exists to avoid. And
Speedometer is DOM/layout/GC-bound, not JS-compute-bound, so even a perfect
JS compiler would barely move it. AOT's leverage is numeric *hotspots*
(crypto, codecs, image/audio, physics), where it delivers the speedups
above — a workload orthogonal to Speedometer.

## Security review

`aotc` is a compiler that ingests untrusted JavaScript and emits C that is
then built, so its own memory safety and the soundness of its accept/decline
decision are security-relevant. This pass reviewed the source and hardened
it; the issues found and fixed:

- **Heap buffer overflow.** The per-pc stack-depth scratch was a fixed
  `malloc(65536)` but indexed by `byte_code_len`, which can exceed it for a
  large function — an out-of-bounds write on attacker-sized input. Now sized
  to the actual maximum bytecode length.
- **Global buffer overflow.** The call-target shadow array was a fixed
  `[8192]` indexed by operand-stack depth (up to `stack_size`, ~65535). Now
  allocated per function to the exact depth.
- **Unsound eligibility fixpoint.** The recursive memoized eligibility check
  could mark one member of a recursion cycle eligible while it transitively
  called an *ineligible* function, which would emit a call to an
  uncompiled/undeclared symbol. Replaced with a monotone greatest-fixpoint
  (assume all eligible, demote anyone calling an unknown/ineligible callee
  until stable) — sound for mutual and cyclic recursion.
- **Silent callee-cap unsoundness.** Callees past a fixed cap were dropped
  without checking their eligibility. Now exceeding the cap declines the
  function.
- **C identifier collisions / injection surface.** JS function names were
  emitted verbatim as C identifiers, so a function named `main`, `pow`,
  `int`, … would collide with the harness, libm, or a C keyword and corrupt
  the generated program. All emitted symbols are now prefixed (`jsfn_…`) and
  the JS name is validated to be a plain C-identifier run, so no JS name can
  influence anything but its own mangled symbol.
- **NULL dereference on higher-order calls.** A function calling a
  function-valued argument or local (e.g. SunSpider's `TimeFunc(func)`)
  passed eligibility but dereferenced a NULL call-target slot at emission —
  a crash on real input. The `validate_kinds` pass proves every call targets
  a statically-known top-level function during analysis, so higher-order
  code is declined cleanly; `find_fn` is also NULL-hardened.
- **One-byte out-of-bounds read.** `JS_AtomToCString` on an anonymous nested
  function's null name atom returns a pointer one past a heap buffer;
  reading the first byte was a 1-byte OOB read, triggered by real-world code
  with anonymous closures (`access-nbody`). Now null-name functions are
  skipped before conversion.
- **Arity mismatch → C compile error or wrong result.** Calls with fewer
  args than the callee's arity could emit a C call with too few arguments
  (compile error) instead of JavaScript's `undefined`→`NaN`. Call emission
  is now arity-aware: missing args are passed as `NaN`, extra args dropped,
  matching the interpreter.
- **Unsound `**` lowering.** The original exponentiation lowered to C `pow`,
  which returns `1` for `(±1) ** ±Infinity` where QuickJS returns `NaN` — a
  silent wrong result. `**` and `Math.pow` now use a `js_pow` helper that
  reproduces QuickJS's `js_math_pow`. The kind-tracking validator
  (`validate_kinds`) additionally closes the soundness holes where a
  function reference or the `Math` object could flow into arithmetic, a
  comparison, a branch condition, or a stored local.

The whole corpus — the self-check programs, the SunSpider sources, a
generated 2000-local stress program, **and all 424 Speedometer 3.1 files
(14 MB)** — was then run through an **AddressSanitizer +
UndefinedBehaviorSanitizer** build of `aotc` with **zero** memory-safety or
UB findings.

The structural security property is unchanged and was re-verified: the
emitted binaries import no `mmap`/`mprotect`/`memfd`, all codegen is
ahead-of-time, and the runtime never needs writable-executable memory.

## Performance

Measured on an Intel Xeon @ 2.80 GHz, gcc 13.3.0 `-O2`. The interpreter is
the in-tree QuickJS built `-O2`; timing excludes process start-up and
parsing on both sides (the hot function is called `reps` times in a warm
loop, timed with a monotonic clock). Reproduce with
`experiments/aot-js/run.sh`.

| benchmark  | what it stresses              | arg     | reps | interp (ms) | AOT (ms) | speedup |
|------------|-------------------------------|---------|------|-------------|----------|---------|
| `fib`      | recursion, calls, compare     | 32      | 1    | 284.0       | 7.8      | **36.6×** |
| `sumloop`  | tight counted loop            | 1000000 | 50   | 1709.4      | 72.1     | **23.7×** |
| `collatz`  | nested loop, `%` (→ `fmod`)   | 20000   | 5    | 406.7       | 105.4    | **3.9×**  |
| `mandel`   | float kernel, `&&`, nested    | 300     | 3    | 777.6       | 28.4     | **27.4×** |
| `cordic`   | fixed-point trig, shifts      | 200000  | 3    | 1052.2      | 35.8     | **29.4×** |
| `popcount` | bit-twiddling, masks/shifts   | 2000000 | 5    | 1043.1      | 392.6    | **2.7×**  |
| `hash`     | FNV hash (`*`, `^`, `>>>`)    | 2000000 | 3    | 431.6       | 173.1    | **2.5×**  |
| `prng`     | xorshift (`<<`, `>>>`, `^`)   | 2000000 | 3    | 406.9       | 223.7    | **1.8×**  |
| `mathkernel` | `Math.{sqrt,sin,cos,pow,…}` | 2000000 | 3    | 2102.0      | 331.4    | **6.3×**  |

Loop/recursion/float kernels win big (24–37×) because every opcode on the
hot path was pure boxing+dispatch overhead that AOT deletes. `collatz` wins
least of the float set (3.9×) because its inner work is dominated by `%`,
which JavaScript defines on doubles and both engines execute as `fmod` — a
cost AOT cannot remove.

The bitwise benchmarks (`popcount`/`hash`/`prng`, 1.8–2.7×) win less because
the interpreter's bitwise path is already a tight native-integer fast path,
whereas AOT must apply `ToInt32`/`ToUint32` to a `double` on every operator.
Profiling the first cut showed those benchmarks *slower* than the
interpreter — the spec `ToInt32` (a `fmod`/`trunc` round-trip) dominated.
Adding an in-range fast path (`if (d >= INT32_MIN && d < 2^31) return
(int32_t)d`) turned `popcount` from 0.8× to 2.7×, `hash` from 0.9× to 2.5×,
and `prng` from 0.5× to 1.8×. The residual gap is the one remaining
`ToInt32` range branch per operator; eliminating it would need value-range
inference (knowing a slot already holds an int32, e.g. after `|0`) — future
work, not done here.

### Where the interpreter time goes

Callgrind on `fib(28)`:

```
991,434,493 (93.48%)  JS_CallInternal     <- the bytecode dispatch loop
 34,967,765 ( 3.30%)  find_own_property   <- variable / global lookups
 30,854,145 ( 2.91%)  JS_FreeValue        <- refcount churn
```

Retired instructions, same workload:

| build       | instructions (Ir) |
|-------------|-------------------|
| interpreter | 1,060,529,150     |
| AOT         | 9,200,069         |

**~115× fewer instructions.** Wall-clock speedup (~35×) is smaller because
the interpreter's instructions are simple and branch-predictable while the
AOT code is bound by real FP latency and call overhead — but the direction
and magnitude are decisive.

### The C compiler does the back-end work

The translator is deliberately naïve; the optimization comes from handing
its output to the C compiler:

| `fib(32)` build | time (ms) | vs interp |
|-----------------|-----------|-----------|
| AOT `cc -O0`    | 27.8      | 10.8×     |
| AOT `cc -O2`    | 7.8       | 35×       |

Even unoptimized AOT beats the interpreter 11×; `-O2` register-allocates
the slots and inlines, roughly quadrupling that. Lowering to C means a
mature optimizer does instruction selection and register allocation for
free — none of which has to be written or security-audited. Build-time
cost is negligible: `aotc` translation is sub-millisecond and `cc -O2` of a
small function is ~5 ms, all paid offline.

## Scope and limitations

The compiler accelerates the **numeric subset**: functions over `double`,
the arithmetic/comparison/logical-not operators, the bitwise and shift
operators (`& | ^ ~ << >> >>>`, with spec-exact `ToInt32`/`ToUint32`),
`Math.*` methods and constants, `if`/`while`/`for`/`do…while`, `++`/`--`,
`?:`, short-circuit `&&`/`||`, direct/mutual/tail recursion, and calls
(including under/over-application) among eligible top-level functions.
Everything else — strings, objects, arrays, property access, `this`,
closures with captured mutable state, non-`Math` built-ins, function-valued
arguments / higher-order code, exceptions, generators, `async`/`await`,
`eval`/`Function` — is **declined and interpreted**. This is a *capability*
boundary, not a correctness one: declining is always safe.

Wiring the AOT path into the live renderer (so eligible functions run
native inside the browser, not just via the standalone tool) is the natural
next step. It is intentionally **not** done here because it cannot be built
or verified in this environment (the browser needs GTK 4 / meson, absent
here) and must not be shipped unverified — the project's definition of done
requires the browser to build and the affected path to work manually. The
in-engine version would add: a `JSValue` calling convention so AOT and
interpreted frames interoperate, an argument type-guard with bailout to the
bytecode for non-numeric callers, and GC-aware handling — all of which
**keep codegen offline**, preserving the no-runtime-W^X property that is the
whole point. `eval`/`Function` stay interpreted by construction: AOT cannot
compile code that does not exist until runtime.

## Reproducing

```sh
cd experiments/aot-js
./selfcheck.sh      # soundness: AOT vs interpreter over the whole corpus
./run.sh            # performance: the benchmark table above
./frameworktest.sh  # real third-party code (SunSpider); needs network
./speedometer.sh    # robustness over the whole Speedometer 3.1 corpus; needs network
./aot-run.sh tests/arith.js arith 6 7   # run one entry, AOT-by-default
```

Requirements: a C compiler and `-lm` (plus `valgrind` for the
instruction-count figures, and network access for `frameworktest.sh`). To
re-run the sanitizer check, build `aotc.c` with
`-fsanitize=address,undefined` and run it over the corpus.

## Files

- `experiments/aot-js/aotc.c` — eligibility analysis, call validation, and bytecode→C lowering.
- `experiments/aot-js/aot-run.sh` — AOT-by-default runner with interpreter fallback.
- `experiments/aot-js/selfcheck.sh` — soundness harness (AOT vs interpreter).
- `experiments/aot-js/frameworktest.sh` — real-world test (SunSpider numeric kernels).
- `experiments/aot-js/speedometer.sh` — robustness/eligibility test over the Speedometer 3.1 corpus.
- `experiments/aot-js/run.sh` — performance benchmark driver.
- `experiments/aot-js/tests/*.js`, `bench/*.js` — corpus and benchmarks.

## Conclusion

A few hundred lines, built on QuickJS's existing front end, turn hot
numeric JavaScript into native code that runs 2–37× faster than the
interpreter — soundly (proven-equivalent or declined), hardened against its
own untrusted input (clean under ASan/UBSan, validated on real third-party
code), and with **all** codegen done ahead of time. For a browser whose
renderer is locked down with `seccomp` and W^X, that last property is the
point: AOT captures a large slice of JIT-class speedup without ever asking
the sandbox for the executable-memory privilege that makes JITs such a rich
source of exploitable bugs.
