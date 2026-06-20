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
./selfcheck.sh                       # soundness: AOT vs interpreter, 82 cases
./run.sh                             # performance benchmark table
./frameworktest.sh                   # real third-party code (SunSpider); needs network
./aot-run.sh tests/arith.js arith 6 7   # run one entry, AOT-by-default
```

`selfcheck.sh` runs every program both through the pure interpreter and
through AOT-by-default and asserts identical results (and that the expected
path — native vs fallback — was taken). `run.sh` reports speedups.
`frameworktest.sh` exercises the compiler on real SunSpider numeric kernels.

The subset covers arithmetic, comparisons, bitwise/shift (`& | ^ ~ << >>
>>>`, spec-exact `ToInt32`/`ToUint32`), all loop forms, `?:`, `&&`/`||`, and
direct/mutual/tail recursion. Anything else (strings, objects, arrays,
`Math.*`, higher-order calls) falls back to the interpreter.

## Contents

- `aotc.c` — eligibility analysis, call validation + bytecode→C lowering (`#include`s `src/quickjs/quickjs.c`).
- `aot-run.sh` — AOT-by-default runner with automatic interpreter fallback.
- `selfcheck.sh` — soundness harness.
- `frameworktest.sh` — real-world test (SunSpider numeric kernels).
- `run.sh` — performance benchmark driver.
- `tests/*.js` — correctness corpus (numeric + must-fall-back programs).
- `bench/*.js` — performance benchmarks.

This is an experiment. It is not built by meson, nothing in `src/` depends
on it, and a stock browser build is unchanged. See the doc for what
wiring it into the live renderer would require (and why that step is not
taken here).
