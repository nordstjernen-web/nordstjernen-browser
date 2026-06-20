# Ahead-of-time JavaScript compilation for QuickJS — a proof of concept

This document records a proof-of-concept (PoC) **ahead-of-time (AOT)**
compiler that turns QuickJS bytecode into native machine code through C,
the performance numbers it produces, and why AOT is a better fit for
Nordstjernen's sandboxed renderer than a traditional **just-in-time
(JIT)** compiler.

The PoC lives in `experiments/aot-js/`. It is an experiment, not a
shipping feature — it compiles a numeric subset of JavaScript. Nothing in
`src/` depends on it and a stock build is unchanged.

## TL;DR

- A small translator (`experiments/aot-js/aotc.c`) reuses the **real
  QuickJS front end** to compile JS to bytecode, then walks that bytecode
  and emits straight-line C. The C is compiled by the system compiler to a
  native binary.
- On four numeric benchmarks the AOT binary runs **3.9×–39×** faster than
  the QuickJS interpreter, with **identical results**.
- For `fib(28)` the AOT code retires **9.2M** instructions versus the
  interpreter's **1.06 billion** — a **115× reduction in work**. Callgrind
  attributes **93%** of interpreter instructions to the bytecode dispatch
  loop (`JS_CallInternal`) alone.
- AOT needs **no writable-and-executable memory at runtime**: code is
  generated at build time. A JIT must `mmap`/`mprotect` pages `PROT_EXEC`
  while running, which is exactly the kind of primitive the renderer's
  `seccomp` sandbox exists to deny. QuickJS today contains **zero**
  `PROT_EXEC`/`mprotect` call sites; AOT keeps it that way.

## Why AOT and not JIT

Nordstjernen runs untrusted JavaScript inside a `seccomp`-filtered,
unprivileged renderer (see `docs/tab-isolation.md`, `docs/watchdog.md`).
The single most dangerous capability a scripting engine can ask of such a
sandbox is the ability to **create executable memory at runtime**. Every
production JS JIT does precisely that: it allocates RW pages, writes
generated machine code into them, and flips them to RX (or keeps them RWX)
with `mprotect`/`VirtualProtect`/`mmap(MAP_JIT)`. That single allowance:

- punches a hole straight through W^X — an attacker who gains a write
  primitive can now stage shellcode in a page that will become executable;
- is the substrate for the entire class of **JIT-spraying** and
  **type-confusion → JIT** exploits that dominate real-world browser CVEs;
- forces the sandbox policy to allow `mprotect(PROT_EXEC)`/`memfd_create`,
  weakening the filter for every other code path too.

AOT moves codegen to **build time**. The machine code is produced by a
trusted, offline C compiler and is mapped from a read-only, signed
executable image like any other program text. At runtime the engine never
writes code, never calls `mprotect(PROT_EXEC)`, and the `seccomp` policy
can keep denying it outright. AOT trades JIT's adaptive, profile-guided
specialization for a strictly smaller attack surface — the right trade for
a security-first browser. (The flip side, spelled out under *Limitations*,
is that AOT cannot specialize on runtime types the way a tiered JIT can.)

The PoC confirms the property concretely: the generated binaries import no
`mmap`/`mprotect`/`memfd` symbols and are pure static numeric code.

```
$ objdump -T mandel | grep -iE 'mprotect|mmap|memfd'
  (no executable-memory syscalls)
$ grep -cE 'mprotect|PROT_EXEC' src/quickjs/quickjs.c
0
```

## How QuickJS compiles JavaScript (what the PoC builds on)

The PoC does **not** re-implement a JS parser. It reuses QuickJS's, which
is worth understanding because it already does most of the hard work. The
pipeline in `src/quickjs/quickjs.c` is:

1. **Parse + emit (`js_parse_program`, `js_parse_*`).** QuickJS is a
   single-pass compiler: the recursive-descent parser emits stack-machine
   bytecode into a growing buffer (`JSFunctionDef.byte_code`) as it goes,
   rather than building a persistent AST. Nested functions become entries
   in the constant pool (`cpool`).
2. **`resolve_variables`.** A second pass that resolves variable
   references to concrete local / argument / closure-variable slots and
   picks the specialized access opcodes (`get_loc8`, `get_arg0`,
   `get_var_ref0`, …).
3. **`resolve_labels`.** Resolves symbolic jump labels to relative byte
   offsets, runs a **peephole optimizer**, and substitutes the compact
   *short opcodes* (`push_0`, `get_loc0`, `if_false8`, `goto8`, …) that
   make the final bytecode dense.
4. **`compute_stack_size`.** Abstract-interprets the bytecode to find the
   maximum operand-stack depth, stored as `stack_size`.
5. **`js_create_function`.** Packages everything into a
   `JSFunctionBytecode` (the struct at `quickjs.c:779`): `byte_code_buf`,
   `cpool`, `vardefs`, `closure_var`, counts, and debug tables.

At run time `JS_CallInternal` (`quickjs.c:17609`) interprets that
bytecode with a **computed-goto threaded dispatcher** (`DIRECT_DISPATCH`).
Each opcode is a label in a 256-entry jump table; every operand is a
tagged `JSValue`, and reference-counted values are `js_dup`/`JS_FreeValue`d
as they move through the stack.

The interpreter is excellent — compact and portable — but it pays three
taxes on every operation that native code does not:

- **dispatch**: a jump-table indirect branch per opcode;
- **boxing**: every value is a tagged `JSValue`, every arithmetic op
  checks tags and may fall back to a slow path;
- **refcount churn**: `js_dup`/`JS_FreeValue` around stack traffic.

The PoC's whole speedup comes from removing these three taxes for code
that provably operates on numbers.

## What the PoC compiler does

`experiments/aot-js/aotc.c` is a single translation unit that
`#include "quickjs.c"` so it can see the internal `JSFunctionBytecode`
layout, the `OP_*` enum, and the `opcode_info[]` size table. Its flow:

1. `JS_Eval(..., JS_EVAL_FLAG_COMPILE_ONLY)` compiles the input with the
   real front end and hands back the top-level function bytecode (a
   `JS_TAG_FUNCTION_BYTECODE` value) without running it.
2. It enumerates the top-level function declarations from the constant
   pool and, for each, walks `byte_code_buf` opcode by opcode.
3. It models the operand stack as a **stack of C expression strings**.
   Every computed value is materialized into a fresh `double tN` temporary,
   so evaluation order and single-evaluation are preserved and the C
   optimizer is free to fold the temporaries away. Jump targets become C
   labels; `if_false`/`goto` become `if (!cond) goto Lx;` / `goto Lx;`.
4. It writes a `.c` file with one `static double` function per JS function
   plus a `main()` that calls the entry point and times it.

The branch-target math mirrors the interpreter exactly: for every jump
opcode the destination is `operand_position + signed_operand`.

### Worked example

Input (`bench/fib.js`):

```js
function fib(n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}
```

QuickJS bytecode (from `QJS_DUMP_FLAGS=1`):

```
get_arg0 0 ; n
push_2 2
lt
if_false8 7
get_arg0 0 ; n
return
7: get_var_ref0 0 ; fib
   get_arg0 0 ; n
   push_1 1
   sub
   call1 1
   get_var_ref0 0 ; fib
   get_arg0 0 ; n
   push_2 2
   sub
   call1 1
   add
   return
```

Emitted C (`aotc`):

```c
static double fib(double a0) {
  double t0 = (a0 < 2);
  if (!(t0)) goto L7;
  return a0;
L7: ;
  double t1 = a0 - 1;
  double t2 = fib(t1);
  double t3 = a0 - 2;
  double t4 = fib(t3);
  double t5 = t2 + t4;
  return t5;
  return 0;
}
```

Self-recursion resolves through the closure variable (`get_var_ref0` →
`fib`); the tagged-value stack machine collapses into plain `double`
arithmetic and a direct call.

## Performance

Measured on an Intel Xeon @ 2.80 GHz, gcc 13.3.0 `-O2`. The interpreter is
the in-tree QuickJS built `-O2`; timing excludes process start-up and
parsing on both sides (the hot function is called `reps` times in a warm
loop, timed with a monotonic clock). Reproduce with
`experiments/aot-js/run.sh`.

| benchmark | what it stresses              | arg     | reps | interp (ms) | AOT (ms) | speedup | result |
|-----------|-------------------------------|---------|------|-------------|----------|---------|--------|
| `fib`     | recursion, calls, compare     | 32      | 1    | 300.9       | 7.8      | **38.6×** | match |
| `sumloop` | tight counted loop            | 1000000 | 50   | 1687.3      | 72.3     | **23.3×** | match |
| `collatz` | nested loop, `%` (→ `fmod`)   | 20000   | 5    | 409.0       | 105.5    | **3.9×**  | match |
| `mandel`  | float kernel, `&&`, nested    | 300     | 3    | 776.1       | 28.5     | **27.3×** | match |

Observations:

- **Loop/recursion/float kernels win big (23–39×)** because every opcode
  in the hot path was pure boxing+dispatch overhead that AOT deletes.
- **`collatz` wins least (3.9×)** because its inner work is dominated by
  the `%` operator, which JavaScript defines on doubles and which both
  engines execute as a `fmod` call — a cost AOT cannot remove. This is the
  honest shape of AOT speedups: they shrink the interpreter tax, not the
  intrinsic cost of the math.

### Where the interpreter time goes

Callgrind on `fib(28)`:

```
991,434,493 (93.48%)  JS_CallInternal     <- the bytecode dispatch loop
 34,967,765 ( 3.30%)  find_own_property   <- variable / global lookups
 30,854,145 ( 2.91%)  JS_FreeValue        <- refcount churn
```

Total retired instructions, same workload:

| build       | instructions (Ir) |
|-------------|-------------------|
| interpreter | 1,060,529,150     |
| AOT         | 9,200,069         |

**~115× fewer instructions.** Wall-clock speedup (≈39×) is smaller than
the instruction-count ratio because the interpreter's instructions are
simple and branch-predictable, while the AOT code is bound by real FP
latency and call overhead — but the direction and magnitude are decisive.

### The C compiler is doing real work

The AOT translator is deliberately naïve (it emits a temporary per value).
The optimization comes from handing that to the C compiler:

| `fib(32)` build | time (ms) | vs interp |
|-----------------|-----------|-----------|
| AOT `cc -O0`    | 27.8      | 10.8×     |
| AOT `cc -O2`    | 7.8       | 38.6×     |

Even unoptimized AOT beats the interpreter 11×; `-O2` register-allocates
the temporaries and inlines, roughly quadrupling that. Translating to C
lets a 40-year-investment optimizer do the back-end work for free — no
register allocator, no instruction selector to write or to audit.

Build-time cost is negligible at this scale: `aotc` translation is
sub-millisecond and `cc -O2` of `fib.c` is ~5 ms. That cost is paid once,
offline, not on the user's machine.

## Limitations (it is a proof of concept)

The PoC handles a **numeric subset**: functions over `double`,
`+ - * / % **`, comparisons, `if`/`while`/`for`, `++`/`--`, recursion, and
calls between top-level functions. It deliberately does **not** handle:

- non-number values — strings, objects, arrays, `undefined`/`null`
  semantics (everything is modelled as `double`);
- closures with captured mutable state, `this`, prototypes, property
  access, `Math.*` and other built-ins;
- exceptions, generators, `async`/`await`;
- short-circuit operators that leave a value on the stack across a branch
  (simple boolean uses in `if`/`while` conditions work; value-producing
  `a && b` expressions are out of scope);
- deoptimization. A real engine cannot assume "this is always a number";
  it needs type guards plus a bytecode fallback, or speculative types with
  a bailout path.

A production AOT path for Nordstjernen would need, at minimum: a type
lattice with guards and an interpreter fallback for cold/megamorphic code,
a `JSValue` calling convention so AOT and interpreted frames interoperate,
GC-aware handling of heap references, and a story for `eval`/`Function`
(which have no ahead-of-time text — they would simply stay interpreted,
since AOT by construction cannot compile code that does not exist until
runtime). None of that changes the security headline: **all codegen still
happens offline, so the runtime never needs writable-executable memory.**

## Reproducing

```sh
cd experiments/aot-js
./run.sh            # builds qjs + aotc, compiles every benchmark, prints the table
```

`run.sh` builds the in-tree QuickJS interpreter and the `aotc` translator,
compiles each `bench/*.js` to a native binary, checks the AOT output
against the interpreter, and prints the timing table. Requirements: a C
compiler and `-lm` (plus `valgrind` for the instruction-count figures).

## Files

- `experiments/aot-js/aotc.c` — the bytecode→C AOT translator.
- `experiments/aot-js/bench/*.js` — benchmark inputs.
- `experiments/aot-js/bench/time_interp.js` — interpreter timing harness.
- `experiments/aot-js/run.sh` — build + verify + benchmark driver.

## Conclusion

A few hundred lines, built directly on QuickJS's existing compiler front
end, turn hot numeric JavaScript into native code that runs 4–39× faster
than the interpreter while producing identical results — and, crucially,
while generating **all** of that code ahead of time. For a browser whose
renderer is locked down with `seccomp` and W^X, that last property is the
point: AOT captures a large slice of JIT-class speedup without ever asking
the sandbox for the executable-memory privilege that makes JITs such a
rich source of exploitable bugs.
