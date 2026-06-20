# aot-js — ahead-of-time JavaScript compiler

A **sound** ahead-of-time compiler that lowers the numeric subset of
JavaScript to native code, by reusing the QuickJS front end to produce
bytecode and translating that bytecode to C. It emits native code only when
the result is provably identical to interpreting it, and otherwise declines
so the caller falls back to the interpreter — so it can never change the
meaning of a program.

AOT is **enabled by default**: `aot-run.sh` always tries AOT first and falls
back to the QuickJS interpreter automatically for anything outside the
subset.

Full write-up — design, the eligibility analysis, the AOT-vs-JIT security
argument, and benchmarks: [`docs/aot-js-compiler.md`](../../docs/aot-js-compiler.md).

## Run

```sh
./selfcheck.sh                       # scalar soundness: AOT vs interpreter, 127 cases
./arraytest.sh                       # typed-array soundness (reads+writes): AOT vs interpreter, 26 cases
./run.sh                             # performance benchmark table
./frameworktest.sh                   # real third-party code (SunSpider); needs network
./speedometer.sh                     # robustness over the Speedometer 3.1 corpus; needs network
./aot-run.sh tests/arith.js arith 6 7   # run one entry, AOT-by-default
```

`selfcheck.sh` runs every program both through the pure interpreter and
through AOT-by-default and asserts identical results (and that the expected
path — native vs fallback — was taken). `run.sh` reports speedups.
`frameworktest.sh` exercises the compiler on real SunSpider numeric kernels;
`speedometer.sh` runs it over the entire Speedometer 3.1 JavaScript corpus
(424 files / 14 MB) as a robustness check.

The subset covers arithmetic, comparisons, bitwise/shift (`& | ^ ~ << >>
>>>`, spec-exact `ToInt32`/`ToUint32`, with int32 range inference that drops
redundant coercions on chained bitwise code), `Math.*` methods and constants
(including `imul`, `clz32`, `hypot`, `fround`), `Float64Array` parameters
read and written (`a[i]`, `a[i] = …`, `a.length`), all loop forms, `?:`,
`&&`/`||`, the comma operator, scalar parameter reassignment, and
direct/mutual/tail recursion. Slot kinds are tracked by a whole-CFG
meet-based dataflow fixpoint, so kinds survive branches and loops. Anything
else (strings, objects, non-`Float64Array` arrays, non-`Math` built-ins,
higher-order calls) falls back to the interpreter. Scalar numeric code runs
2.4×–63× the interpreter; array kernels (dot, norm, stencil, in-place
scale/axpy) 15×–29×.

## Contents

- `aotc.c` — eligibility analysis, kind-tracking validator, array classification + bytecode→C lowering (`#include`s `src/quickjs/quickjs.c`).
- `aot-run.sh` — AOT-by-default runner with automatic interpreter fallback.
- `selfcheck.sh` — scalar soundness harness.
- `arraytest.sh` — typed-array soundness harness.
- `frameworktest.sh` — real-world test (SunSpider numeric kernels).
- `speedometer.sh` — robustness/eligibility test over the Speedometer 3.1 corpus.
- `run.sh` — performance benchmark driver.
- `tests/*.js` — correctness corpus (numeric + must-fall-back programs).
- `bench/*.js` — performance benchmarks.

This is an experiment. It is not built by meson, nothing in `src/` depends
on it, and a stock browser build is unchanged. See the doc for what
wiring it into the live renderer would require (and why that step is not
taken here).
