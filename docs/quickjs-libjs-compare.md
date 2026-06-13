# QuickJS vs. LibJS — JavaScript feature comparison

This note compares the JavaScript engine bundled with Nordstjernen
([quickjs-ng](https://github.com/quickjs-ng/quickjs), vendored at
`src/quickjs/`) against [LibJS](https://github.com/LadybirdBrowser/ladybird/tree/master/Libraries/LibJS),
the clean-room ECMAScript engine in the Ladybird browser. The goal is to
identify language and built-in surface that LibJS implements but the
bundled QuickJS does **not**, so we know what a site might exercise that
our engine cannot service.

This is a source-level comparison (LibJS `master`, June 2026) — it lists
*presence* of features, not conformance depth. Both engines are
independent implementations; QuickJS is a compact ES2023+ interpreter,
LibJS is a spec-tracking engine that passes >90% of test262.

## Versions compared

| Engine  | Identity | Source |
| ------- | -------- | ------ |
| QuickJS | quickjs-ng **0.15.1** (`QJS_VERSION_*` in `src/quickjs/quickjs.h`) | in-tree fork at `src/quickjs/` |
| LibJS   | Ladybird `master` (June 2026) | upstream, not vendored |

## What the bundled QuickJS already covers

For context — these are present in `src/quickjs/quickjs.c` and need no
LibJS to match, so they are explicitly **out of scope** for the gap list
below:

- Full ES2015–ES2023 core: `let`/`const`, classes (incl. private
  fields/methods, static blocks), destructuring, generators,
  async/await, async generators, modules (`import`/`export`, dynamic
  `import()`, `import.meta`), `Proxy`, `Reflect`, `Symbol`, `BigInt`.
- `WeakRef`, `FinalizationRegistry`, `WeakMap`/`WeakSet`.
- `SharedArrayBuffer` + `Atomics`.
- `Float16Array` / `DataView` float16 / `Math.f16round`.
- Explicit Resource Management: `using`/`await using`, `Symbol.dispose`,
  `Symbol.asyncDispose`, `DisposableStack`, `AsyncDisposableStack`.
- Iterator Helpers: `Iterator.prototype.{map,filter,take,drop,flatMap,
  reduce,toArray,forEach,some,every,find}`, plus `Iterator.concat` and
  the iterator-zip helpers.
- `RegExp.escape`, `Object.groupBy` / `Map.groupBy`,
  `Array.prototype.{findLast,findLastIndex,with,toSorted,toReversed,
  toSpliced}`, `Array.fromAsync`.
- `JSON.parse` source access / `JSON.rawJSON` / `JSON.isRawJSON`.
- `DOMException` as a built-in class, `Error.prototype.stack` /
  call-site machinery.

## Features in LibJS that the bundled QuickJS does NOT have

Three major areas account for essentially the entire gap. All three are
absent from `src/quickjs/` — there are no atoms, classes, or
intrinsics for them (`grep -ci "Intl\|Temporal\|ShadowRealm"
src/quickjs/quickjs.c` returns 0).

### 1. Intl — Internationalization API (ECMA-402)

QuickJS ships **no `Intl` object at all**. LibJS implements the full
ECMA-402 surface (backed by ICU), under
`Libraries/LibJS/Runtime/Intl/`:

| `Intl` constructor      | Purpose |
| ----------------------- | ------- |
| `Intl.Collator`         | Locale-aware string comparison |
| `Intl.DateTimeFormat`   | Locale-aware date/time formatting |
| `Intl.DisplayNames`     | Localized names for languages/regions/scripts/currencies |
| `Intl.DurationFormat`   | Locale-aware duration formatting |
| `Intl.ListFormat`       | Locale-aware list joining ("a, b, and c") |
| `Intl.Locale`           | Locale identifier object model |
| `Intl.NumberFormat`     | Number/currency/unit/percent formatting |
| `Intl.PluralRules`      | Plural category selection |
| `Intl.RelativeTimeFormat` | "3 days ago" style formatting |
| `Intl.Segmenter`        | Grapheme/word/sentence segmentation |

Knock-on effect: the locale-sensitive methods that *do* exist in QuickJS
(`String.prototype.localeCompare`, `toLocaleLowerCase`/`UpperCase`,
`Number.prototype.toLocaleString`, `Date.prototype.toLocale*String`) are
present but **not locale-aware** — they fall back to default/`en`-ish
behaviour. LibJS routes these through Intl/ICU for correct localized
output.

### 2. Temporal — modern date/time API

QuickJS has **no Temporal implementation**. (The Nordstjernen shell
injects an *empty* `Temporal` object in `src/js.c` —
`ns_set_if_missing(ctx, global, "Temporal", JS_NewObject(ctx))` — so
`typeof Temporal === "object"` is true, but it has no usable members.
This is a feature-detection cushion, not an implementation.)

LibJS implements the full proposal under
`Libraries/LibJS/Runtime/Temporal/`:

| Temporal type            | Purpose |
| ------------------------ | ------- |
| `Temporal.Instant`       | Exact point on the timeline (ns precision) |
| `Temporal.ZonedDateTime` | Instant + time zone + calendar |
| `Temporal.PlainDate`     | Calendar date, no time/zone |
| `Temporal.PlainTime`     | Wall-clock time, no date/zone |
| `Temporal.PlainDateTime` | Date + time, no zone |
| `Temporal.PlainYearMonth`| Year + month |
| `Temporal.PlainMonthDay` | Month + day |
| `Temporal.Duration`      | Length of time |
| `Temporal.Now`           | Current instant/date/time accessors |

plus the supporting calendar, time-zone, and ISO-8601 parsing
machinery.

### 3. ShadowRealm — isolated execution context

QuickJS has **no `ShadowRealm`**. LibJS implements the ShadowRealm
constructor (`Libraries/LibJS/Runtime/ShadowRealm*`), providing a fresh
global environment with `evaluate()` and `importValue()` for
synchronous, isolated evaluation. QuickJS can create independent
`JSContext`s at the C API level, but exposes no script-visible
`ShadowRealm` binding.

## Summary

| Feature area              | QuickJS-ng 0.15.1 | LibJS |
| ------------------------- | :---------------: | :---: |
| Core ES2023+ language     | ✅ | ✅ |
| WeakRef / FinalizationRegistry | ✅ | ✅ |
| SharedArrayBuffer / Atomics | ✅ | ✅ |
| Float16 typed arrays      | ✅ | ✅ |
| Explicit Resource Mgmt (`using`) | ✅ | ✅ |
| Iterator Helpers          | ✅ | ✅ |
| **Intl (ECMA-402)**       | ❌ | ✅ |
| **Temporal**              | ❌ (empty stub) | ✅ |
| **ShadowRealm**           | ❌ | ✅ |

The practical gap is **i18n and dates**: Intl and Temporal are the two
features real-world sites are most likely to reach for and find missing.
ShadowRealm is rarely used on the public web today.

## Implications for Nordstjernen

- **Intl** is the highest-impact gap. A site calling
  `new Intl.NumberFormat(...)` or `new Intl.DateTimeFormat(...)` throws
  `TypeError: Intl is not defined`. A future option is a minimal,
  ICU-free `Intl` shim covering the common formatters in the default
  locale; a full implementation would mean an ICU (or equivalent)
  dependency, which weighs against the project's "no bloat" constraint.
- **Temporal** currently presents an empty object, which is arguably
  worse than absent for feature detection (`Temporal.Now` is
  `undefined`, not a clear `ReferenceError`). If we do not intend to
  implement it, consider removing the stub so detection libraries see a
  clean absence; if we do, it is a large, self-contained body of work.
- **ShadowRealm** is low priority given negligible real-world use.
