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
  the subset. Results are identical either way.
- A 55-case self-check (`selfcheck.sh`) compares AOT against the
  interpreter across arithmetic, short-circuit logic, ternaries, loops,
  `do/while`, mutual recursion, float math, division-by-zero, negative
  modulo and fractional powers — **all identical**, and out-of-subset
  programs (strings, `Math.*`, arrays) correctly fall back.
- It runs **3.8×–35×** faster than the interpreter on numeric workloads;
  for `fib(28)` it retires **9.2M** instructions versus the interpreter's
  **1.06 billion**. Callgrind attributes **93%** of interpreter
  instructions to the bytecode dispatch loop alone.
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
   the single source of truth), every pushed constant is numeric, and every
   variable/closure reference names a known top-level function (the only
   non-number the subset permits, and only as a call target). A separate
   abstract interpretation (`compute_sp`) computes the operand-stack depth
   at every program point and rejects the function if any opcode is
   unsupported or the depth is inconsistent across a control-flow join. A
   function is finally **eligible** only if it is locally OK *and every
   function it calls is eligible* — a fixpoint over the call graph that
   handles direct and mutual recursion. Anything that fails is declined.

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

JavaScript numbers *are* IEEE-754 doubles, and the whitelisted operators
(`+ - * / % ** < <= > >= == != !`) are defined on them with exactly the
semantics C gives `double`: `%` is `fmod`, `**` is `pow`, division by zero
yields `±Inf`/`NaN`, comparisons yield `0`/`1`. The eligibility gate
guarantees the only values flowing through a compiled function are numbers
(numeric constants, numeric args, and results of numeric ops) — so the
`double` model is not an approximation, it is the spec. Operators that
would require integer coercion with non-`double` semantics (`& | ^ << >>
>>>`, which apply `ToInt32`) are deliberately **not** whitelisted; a
program using them is declined and interpreted.

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
division by zero (`±Inf`/`NaN`), negative modulo, and fractional powers,
plus three programs (strings, `Math.*`, arrays) that must fall back.

```
passed: 55   failed: 0
```

Every eligible case matches the interpreter bit-for-bit; every ineligible
case falls back and still matches. That is the soundness guarantee made
concrete.

## Performance

Measured on an Intel Xeon @ 2.80 GHz, gcc 13.3.0 `-O2`. The interpreter is
the in-tree QuickJS built `-O2`; timing excludes process start-up and
parsing on both sides (the hot function is called `reps` times in a warm
loop, timed with a monotonic clock). Reproduce with
`experiments/aot-js/run.sh`.

| benchmark | what it stresses              | arg     | reps | interp (ms) | AOT (ms) | speedup |
|-----------|-------------------------------|---------|------|-------------|----------|---------|
| `fib`     | recursion, calls, compare     | 32      | 1    | 272.1       | 7.8      | **35.1×** |
| `sumloop` | tight counted loop            | 1000000 | 50   | 1673.7      | 72.1     | **23.2×** |
| `collatz` | nested loop, `%` (→ `fmod`)   | 20000   | 5    | 411.6       | 107.0    | **3.8×**  |
| `mandel`  | float kernel, `&&`, nested    | 300     | 3    | 772.7       | 28.4     | **27.2×** |

Loop/recursion/float kernels win big (23–35×) because every opcode on the
hot path was pure boxing+dispatch overhead that AOT deletes. `collatz` wins
least (3.8×) because its inner work is dominated by `%`, which JavaScript
defines on doubles and which both engines execute as `fmod` — a cost AOT
cannot remove. That is the honest shape of AOT speedups: they shrink the
interpreter tax, not the intrinsic cost of the math.

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
the arithmetic/comparison/logical-not operators above, `if`/`while`/`for`/
`do…while`, `++`/`--`, `?:`, short-circuit `&&`/`||`, direct/mutual/tail
recursion, and calls among eligible top-level functions. Everything else —
strings, objects, arrays, property access, `this`, closures with captured
mutable state, `Math.*` and other built-ins, bitwise/shift operators,
exceptions, generators, `async`/`await`, `eval`/`Function` — is **declined
and interpreted**. This is a *capability* boundary, not a correctness one:
declining is always safe.

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
./aot-run.sh tests/arith.js arith 6 7   # run one entry, AOT-by-default
```

Requirements: a C compiler and `-lm` (plus `valgrind` for the
instruction-count figures).

## Files

- `experiments/aot-js/aotc.c` — the eligibility analysis and bytecode→C lowering.
- `experiments/aot-js/aot-run.sh` — AOT-by-default runner with interpreter fallback.
- `experiments/aot-js/selfcheck.sh` — soundness harness (AOT vs interpreter).
- `experiments/aot-js/run.sh` — performance benchmark driver.
- `experiments/aot-js/tests/*.js`, `bench/*.js` — corpus and benchmarks.

## Conclusion

A few hundred lines, built on QuickJS's existing front end, turn hot
numeric JavaScript into native code that runs 4–35× faster than the
interpreter — soundly (proven-equivalent or declined), and with **all**
codegen done ahead of time. For a browser whose renderer is locked down
with `seccomp` and W^X, that last property is the point: AOT captures a
large slice of JIT-class speedup without ever asking the sandbox for the
executable-memory privilege that makes JITs such a rich source of
exploitable bugs.
