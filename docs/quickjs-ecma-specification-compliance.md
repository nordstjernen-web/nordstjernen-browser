# QuickJS ECMAScript specification compliance

This document tracks deliberate, browser-side compliance work on the
bundled QuickJS engine (`src/quickjs/`, forked from
[quickjs-ng](https://github.com/quickjs-ng/quickjs)) against the
ECMAScript Language Specification ([ECMA-262](https://tc39.es/ecma262/))
and its companion specs (ECMA-402 Intl, Temporal).

QuickJS-ng is already extremely conformant — a freshly synced tree
(currently `0.15.1` + post-release master) passes the overwhelming
majority of [test262](https://github.com/tc39/test262). The work
recorded here closes the *remaining* known gaps, prioritised by ROI:
small, localised, well-specified fixes that we can verify directly with
the in-tree `qjs` host tool (`builddir/src/quickjs/qjs.exe`).

## How we verify

There is no test262 checkout in the tree. Each fix is reproduced with a
minimal script driven through the standalone interpreter, e.g.:

```sh
./builddir/src/quickjs/qjs.exe -e '<repro>'
```

The known-failing baseline is captured in
`src/quickjs/test262_errors.txt` (the list quickjs-ng ships as
"expected" failures). Categorising it gives the working backlog:

| Cluster | Subtests | Difficulty | Status |
| --- | --- | --- | --- |
| TypedArray `[[Set]]` value-coercion on invalid index | ~12 | low | **done** |
| `with` / Proxy / `Symbol.unscopables` trap ordering | ~10 | medium | open |
| AsyncFromSyncIterator close ordering on rejected promises | ~8 | medium | open |
| TypedArray `subarray` species-ctor argument list | 4 | low | **done** |
| TypedArray `subarray`/`slice` detach + species offset | ~4 | medium | open |
| RegExp `v` flag (unicodeSets) property escapes | ~10 | high | open |
| RegExp `\p{Script=Unknown}` value | 6 | medium | open |
| Class field named `get`/`set` + generator (ASI) | 2 | low | **done** |
| Destructuring / computed-key evaluation order | ~8 | medium | open |
| Module ambiguous star-export resolution | ~6 | medium | open |
| AnnexB CallExpression assignment-target type | 7 | medium | open |

## Changes

### 1. TypedArray `[[Set]]` must not coerce the value for an invalid index when the receiver differs

**Spec:** [`[[Set]]` for Integer-Indexed exotic objects](https://tc39.es/ecma262/#sec-typedarray-set) /
`TypedArraySetElement`. The value is passed through `ToNumber` /
`ToBigInt` (observable via a `valueOf`/`toString` side effect) **only**
when `SameValue(O, Receiver)` is true. When the index is an
out-of-bounds canonical numeric index and the receiver is *not* the
typed array (e.g. `Reflect.set(ta, 10, v, otherReceiver)`, a primitive
receiver, or a typed array reached through the prototype chain),
`IsValidIntegerIndex` is false and `[[Set]]` returns `true` without
touching the value.

**Bug:** `JS_SetPropertyInternal2`'s `typed_array_oob` path coerced the
value unconditionally ("evaluate value for side effects"), so a
`valueOf` was wrongly invoked whenever the receiver differed from the
typed array.

**Fix:** guard the coercion with `p == p1` (receiver is the typed array
itself). `src/quickjs/quickjs.c`, `JS_SetPropertyInternal2`.

Covers test262:
`built-ins/TypedArrayConstructors/internals/Set/key-is-canonical-invalid-index-{reflect,prototype-chain}-set.js`
(plain + BigInt) and
`.../Set/key-is-out-of-bounds-receiver-is-not-{object,typed-array}.js`.

Repro (all coerce=false except the last):

```js
var ta = new Int32Array(1), seen;
var v = { valueOf() { seen = true; return 1; } };
seen = false; Reflect.set(ta, 10, v, {});            // false
seen = false; Reflect.set(ta, 10, v, 5);             // false (primitive receiver)
seen = false; Object.create(ta)[10] = v;             // false (prototype chain)
seen = false; Reflect.set(ta, 10, v, ta);            // true  (receiver IS ta)
```

### 2. `%TypedArray%.prototype.subarray` omits the length argument for a length-tracking source

**Spec:** [`%TypedArray%.prototype.subarray`](https://tc39.es/ecma262/#sec-%typedarray%.prototype.subarray)
step 13: when `O.[[ArrayLength]]` is `auto` (a length-tracking view over a
resizable `ArrayBuffer`) **and** `end` is `undefined`, the argument list
handed to `TypedArraySpeciesCreate` is `« buffer, 𝔽(beginByteOffset) »` —
two arguments. Only when an explicit `end` is given (step 14) is the
computed `newLength` appended as a third argument.

**Bug:** `js_typed_array_subarray` always passed four entries to
`js_typed_array___speciesCreate` (which forwards all but the first), so a
length-tracking `subarray(start)` invoked the species constructor with
`(buffer, byteOffset, undefined)` instead of `(buffer, byteOffset)`.

**Fix:** call `js_typed_array___speciesCreate` with `argc == 3` (forwards
two arguments) in the length-tracking, `end`-undefined case, otherwise
`argc == 4`. `src/quickjs/quickjs.c`, `js_typed_array_subarray`.

Covers test262
`built-ins/TypedArray/prototype/subarray/speciesctor-get-species-custom-ctor-invocation.js`
(plain + BigInt).

### 3. Class field named `get`/`set` followed by a generator method (ASI)

**Spec:** [Class definitions](https://tc39.es/ecma262/#sec-class-definitions)
grammar. `get` and `set` introduce an accessor `MethodDefinition` only
when a `ClassElementName` follows. A getter/setter can never be a
generator, so `get` (or `set`) followed by `*` cannot be an accessor.
When the `*` is on the next line, ASI terminates a `FieldDefinition`, so

```js
class C {
  get
  *gen() {}
}
```

is a field named `get` followed by a generator method `gen` — both valid.

**Bug:** `js_parse_property_name` treated any token after `get`/`set`
other than `: , } ( = ;` as the start of an accessor name, so it tried to
parse `*gen` as the getter's property name and raised
`SyntaxError: invalid property name`.

**Fix:** in a class body (`allow_private`), when `get`/`set` is followed
by `*` with an intervening line terminator (`s->got_lf`), treat the
keyword as a field name. The same construct without the line terminator
(`get *gen(){}`) still has no ASI and stays a `SyntaxError`, and a real
accessor (`get foo(){}`, even split across lines) is unaffected.
`src/quickjs/quickjs.c`, `js_parse_property_name`.

Covers test262
`language/statements/class/elements/syntax/valid/grammar-field-named-{get,set}-followed-by-generator-asi.js`.
