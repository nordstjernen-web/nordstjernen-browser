# aot-js — proof-of-concept AOT JavaScript compiler

A proof of concept that compiles a numeric subset of JavaScript **ahead of
time** to native code, by reusing the QuickJS front end to produce
bytecode and then translating that bytecode to C.

Full write-up, motivation (AOT vs JIT security), and benchmark results:
[`docs/aot-js-compiler.md`](../../docs/aot-js-compiler.md).

## Run

```sh
./run.sh
```

Builds the in-tree QuickJS interpreter and the `aotc` translator, compiles
each `bench/*.js` both ways, verifies the AOT result matches the
interpreter, and prints a timing table.

## Contents

- `aotc.c` — the bytecode→C translator (`#include`s `src/quickjs/quickjs.c`).
- `bench/*.js` — numeric benchmarks (`fib`, `sumloop`, `collatz`, `mandel`).
- `bench/time_interp.js` — interpreter timing harness.
- `run.sh` — build + verify + benchmark driver.

This is an experiment. It is not built by meson, nothing in `src/` depends
on it, and a stock browser build is unchanged.
