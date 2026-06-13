# QuickJS vs. LibJS — JavaScript feature comparison

This note compares the JavaScript engine bundled with Nordstjernen
([quickjs-ng](https://github.com/quickjs-ng/quickjs), vendored at
`src/quickjs/`) against [LibJS](https://github.com/LadybirdBrowser/ladybird/tree/master/Libraries/LibJS),
the clean-room ECMAScript engine in the Ladybird browser. The goal is to
identify language and built-in surface that LibJS implements but the
bundled QuickJS does **not**, so we know what a site might exercise that
our engine cannot service.

This is a source-level comparison (LibJS `master`, re-verified
2026-06-13) — it lists *presence* of features, not conformance depth.
Both engines are independent implementations; QuickJS is a compact
ES2023+ interpreter that also tracks the ES2024/ES2025 proposal set,
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
- Set methods: `union`, `intersection`, `difference`,
  `symmetricDifference`, `isSubsetOf`, `isSupersetOf`, `isDisjointFrom`.
- `Promise.try`; `Error.isError`; `Array.fromAsync`.
- `Uint8Array` base64/hex: `fromBase64`/`toBase64`/`setFromBase64` and
  `fromHex`/`toHex`/`setFromHex`.
- `String.prototype.isWellFormed` / `toWellFormed`.
- Resizable / transferable `ArrayBuffer` (`maxByteLength`, `resize`,
  `transfer`, `transferToFixedLength`).
- RegExp `/v` flag (`unicodeSets`).
- `Math.sumPrecise`; `Atomics.pause`.
- Import attributes syntax (`import … with { type: … }` is parsed).
- `Promise.withResolvers`; `Map.prototype.getOrInsert` /
  `getOrInsertComputed` (upsert).
- Symbols as `WeakMap`/`WeakSet` keys; `RegExp` `/d` match indices
  (`hasIndices`); `Iterator.prototype[Symbol.dispose]`.

So the bundled QuickJS is current through the ES2024/ES2025 proposal set;
the gap below is genuinely narrow.

## Features in LibJS that the bundled QuickJS does NOT have

After auditing the recent-proposal surface (the list above), the gap
narrows to three areas absent from `src/quickjs/` itself but present in
LibJS: **Intl**, **Temporal**, and **JSON modules**. **Nordstjernen now
supplies all three** outside the QuickJS core — Intl and Temporal as
native C built-ins, JSON modules in the module loader — so the only
remaining shortfall against LibJS is the *conformance depth* of the i18n
data (see notes below), not any missing API.

> Note (re-verify 2026-06-13): an earlier revision listed **ShadowRealm**
> as a LibJS-only gap. That was wrong — direct file checks against LibJS
> `master` show no `ShadowRealm*` in `Libraries/LibJS/Runtime/` (nor
> `AsyncContext`), so ShadowRealm is a *shared* gap (neither engine
> implements it), like decorators. See "Checked, and *not* a gap".

### 1. Intl — Internationalization API (ECMA-402)

QuickJS-ng ships **no `Intl` object at all**. LibJS implements the full
ECMA-402 surface backed by ICU, under `Libraries/LibJS/Runtime/Intl/`:

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

**Nordstjernen status: implemented natively in `src/js_intl.c`.** All ten
constructors plus `getCanonicalLocales` and `supportedValuesOf` are
provided over the public QuickJS C API, ICU-free. `Number/Date/String`'s
`toLocaleString`/`toLocale*String`/`localeCompare` are wired through it,
so they are now locale-aware (`localeCompare` uses GLib's Unicode
casefold/normalize/collate; `Segmenter` uses Pango grapheme/word/sentence
boundaries; `PluralRules` ships the common CLDR rule families). This
supersedes — and removed — the older JavaScript `Intl` polyfill in
`data/js/polyfills.js`, and adds `DisplayNames` and `DurationFormat`,
which that polyfill lacked.

The deliberate limitation versus LibJS is **data depth, not API
coverage**: without ICU/CLDR, locale-specific month/weekday names,
display names, and number/currency symbols come from compact built-in
tables centred on English and a set of common European locales, with
graceful fallback. Formatting is correct for the common cases; it is not
full ECMA-402 conformance for every locale. This is the intended trade-off
under the project's no-ICU/no-bloat constraint.

### 2. Temporal — modern date/time API

QuickJS-ng has **no Temporal implementation**. LibJS implements the full
proposal under `Libraries/LibJS/Runtime/Temporal/`:

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

**Nordstjernen status: implemented natively in `src/js_date.c`.** All
nine types above are provided, sharing the civil-date math in
`src/datetime.c`: `from()` (ISO-string and property-bag forms),
`toString()`/`toJSON()`, the full getter surface (`year`, `monthCode`,
`dayOfWeek`, `daysInMonth`, `inLeapYear`, `weekOfYear`, …),
`add`/`subtract` (with ISO calendar month-overflow constrain and
time/day carry), `with`, `until`/`since`, `equals`/`compare`, the
`toPlainDate`/`toPlainTime`/`toPlainDateTime`/`toInstant` conversions,
and `Temporal.Now`. This replaces the former empty `Temporal` stub.

Deliberate limitations versus LibJS: the calendar is **ISO-8601 only**
(no Hebrew/Islamic/Japanese/… calendars); time zones are limited to
**UTC and fixed offsets** (no IANA tz database, so `Now.*ISO()` and
`ZonedDateTime` are UTC-based); `Instant`/`ZonedDateTime` carry
nanosecond precision in a signed 64-bit nanosecond field (≈ years
1678–2262); and `round()`/`total()`/`largestUnit`-style `since`/`until`
balancing are not implemented. The common date arithmetic and
formatting paths are correct.

### 3. JSON modules

LibJS evaluates **JSON modules** (`import data from "./x.json" with
{ type: "json" }`) through `SyntheticModule` — `parse_json_module` /
`create_default_export_synthetic_module` build a module whose `default`
export is the parsed JSON. The bundled QuickJS *parses* the import-
attributes syntax (`js_parse_with_clause`) but provides **no JSON
module synthesis** — there is no `parse_json_module`/synthetic-module
path, so the QuickJS *engine* does not turn a `type: "json"` import into a
usable module by itself.

**Nordstjernen status: implemented in the module loader
(`src/js.c::ns_js_module_loader`).** The loader now uses
`JS_SetModuleLoaderFunc2`, which delivers the import attributes; when
`type: "json"` is present it parses the fetched bytes with `JS_ParseJSON`
and builds a synthetic `JS_NewCModule` whose sole `default` export is the
parsed value. Both static `import x from "…" with { type: "json" }` and
dynamic `import("…", { with: { type: "json" } })` work, over `data:` and
`http(s)` specifiers, and a malformed payload fails the import cleanly.
(The CSS/Wasm/HTML module variants remain unimplemented and are rarely
used on the open web.)

### Checked, and *not* a gap

To keep the comparison honest, these were verified present in the bundled
QuickJS and so are **not** LibJS-only: Set methods, `Promise.try`,
`Promise.withResolvers`, `Map.prototype.getOrInsert`, `Error.isError`,
`Uint8Array` base64/hex, well-formed-string methods,
resizable/transferable `ArrayBuffer`, RegExp `/v` and `/d` flags,
symbols as weak-collection keys, `Iterator.prototype[Symbol.dispose]`,
`Math.sumPrecise`, `Atomics.pause`, and import-attributes parsing.
**Shared gaps** — absent from QuickJS *and* from LibJS, so not
differentiators: **ShadowRealm** (no `ShadowRealm*` in LibJS `Runtime/`,
verified 2026-06-13), **AsyncContext**, and **decorators** (`@decorator`;
not in LibJS's current Rust parser). RegExp inline modifiers (`(?i:…)`)
live in Ladybird's separate LibRegex, outside the LibJS-vs-QuickJS scope
of this note.

## Summary

The QuickJS column is the bare engine; the Nordstjernen column reflects
what the browser exposes after its native C additions load.

| Feature area              | QuickJS-ng 0.15.1 | Nordstjernen | LibJS |
| ------------------------- | :---------------: | :----------: | :---: |
| Core ES2023+ language     | ✅ | ✅ | ✅ |
| WeakRef / FinalizationRegistry | ✅ | ✅ | ✅ |
| SharedArrayBuffer / Atomics | ✅ | ✅ | ✅ |
| Float16 typed arrays      | ✅ | ✅ | ✅ |
| Explicit Resource Mgmt (`using`) | ✅ | ✅ | ✅ |
| Iterator Helpers          | ✅ | ✅ | ✅ |
| Set methods / `Promise.try` / U8 base64-hex / resizable AB | ✅ | ✅ | ✅ |
| Decorators (`@`) / ShadowRealm / AsyncContext | ❌ | ❌ | ❌ |
| **Intl (ECMA-402)**       | ❌ | ✅ native, ICU-free | ✅ ICU |
| **Temporal**              | ❌ | ✅ native (ISO/UTC) | ✅ full |
| **JSON modules**          | ❌ (attr parsed) | ✅ loader | ✅ |

Nordstjernen now matches LibJS on every API surface in this table; the
only remaining shortfall is the i18n/calendar/time-zone *data depth* that
an ICU-backed engine provides and an ICU-free one deliberately does not.
ShadowRealm, AsyncContext, and decorators are absent from *both* engines.
Everything else in the recent-proposal set is already present in the
bundled QuickJS.

## Implications for Nordstjernen

- **Intl** and **Temporal** are now provided natively in C
  (`src/js_intl.c`, `src/js_date.c`) and verified through the headless
  `--eval` harness. Sites calling `new Intl.NumberFormat(...)`,
  `Intl.DateTimeFormat`, `Temporal.Now.plainDateISO()`, etc. work. The
  ICU-free trade-off is reduced locale/calendar/time-zone fidelity, not
  missing API surface.
- A future ICU/CLDR integration could raise i18n fidelity to full
  ECMA-402 conformance, but weighs against the project's no-bloat
  constraint; the current tables-based approach is the chosen balance.
- **JSON modules** are now handled in `ns_js_module_loader`
  (`JS_SetModuleLoaderFunc2` + `JS_ParseJSON` + a synthetic
  `JS_NewCModule` default export), verified for static and dynamic
  imports over `data:`/`http(s)`.
- **ShadowRealm** is not implemented by either engine, so there is no
  gap to close versus LibJS; it is low priority anyway given negligible
  real-world web use. QuickJS can create isolated `JSContext`s at the C
  level if a script-visible binding is ever wanted.
