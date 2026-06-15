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
| `with` GetBindingValue re-probe (read trap order + strict) | ~5 | medium | **done** |
| `with` SetMutableBinding re-probe (write trap order) | ~3 | medium | open (needs a dedicated with-store opcode) |
| AsyncFromSyncIterator close on rejection (`closeOnRejection`) | ~8 | medium | **done** |
| TypedArray `subarray` species-ctor argument list | 4 | low | **done** |
| TypedArray `subarray`/`slice` detach + species offset | ~4 | medium | open |
| RegExp `v` flag — Unicode semantics (property escapes, `\u{}`, casing) | ~8 | medium | **done** |
| RegExp `v` flag — set operations / strings (`&&`, `--`, `\q{}`, RGI_Emoji) | ~4 | high | open |
| RegExp `\p{Script=Unknown}` value | 6 | medium | open |
| Class field named `get`/`set` + generator (ASI) | 2 | low | **done** |
| Object computed-key `ToPropertyKey` before value | 2 | low | **done** |
| Destructuring assignment-target evaluation order | ~6 | medium | open |
| Module star-export of the same namespace is unambiguous | ~5 | medium | **done** |
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

### 4. `with` GetBindingValue re-probes the binding object

**Spec:** [Object Environment Records `GetBindingValue`](https://tc39.es/ecma262/#sec-object-environment-records-getbindingvalue-n-s).
After `HasBinding` (which itself does `HasProperty` + reads
`@@unscopables`), `GetBindingValue(N, S)` performs a *second*
`HasProperty(bindingObject, N)`; if that is false the result is a
`ReferenceError` when `S` is true and `undefined` otherwise. The
`@@unscopables` getter can delete the binding between the two probes.

**Bug:** the `OP_with_get_var` / `OP_with_get_ref` / `OP_with_get_ref_undef`
interpreter cases went straight from the `@@unscopables` check to
`[[Get]]`, skipping the `GetBindingValue` `HasProperty`. With a Proxy
binding object the second `has` trap was missing; with a deleted binding
the strict-mode `ReferenceError` was not raised.

**Fix:** new helper `js_with_get_binding_value` does `HasProperty` then
either `[[Get]]`, returns `undefined` (sloppy), or throws a
`ReferenceError` (strict, taken from the executing frame's
`is_strict_mode` — `with` bodies are sloppy but may contain nested strict
functions/evals). Used by the three read opcodes.
`src/quickjs/quickjs.c`, interpreter `OP_with_*` and the new helper.

Covers test262
`language/statements/with/get-binding-value-{idref,call}-with-proxy-env.js`
and `.../get-mutable-binding-binding-deleted-in-get-unscopables-strict-mode.js`.

The write side (`SetMutableBinding`,
`language/statements/with/set-mutable-binding-*`) is not yet fixed: a
`with` assignment `p = 1` lowers to `OP_with_make_ref` + `OP_put_ref_value`,
and `OP_put_ref_value` is shared with non-`with` reference stores (function
-name dummy objects, captured-local ref objects), so the extra
`HasProperty` can't be added there unconditionally. It needs a dedicated
`with`-store opcode.

### 5. Object literal computed key is `ToPropertyKey`-converted before the value

**Spec:** [`PropertyDefinition : PropertyName : AssignmentExpression`](https://tc39.es/ecma262/#sec-runtime-semantics-propertydefinitionevaluation).
The `PropertyName` is evaluated first — and `Evaluation of ComputedPropertyName`
includes `? ToPropertyKey(propName)` — *before* the value
`AssignmentExpression` is evaluated.

**Bug:** `js_parse_object_literal` emitted the key expression, then the
value expression, then `OP_define_array_el` (which performs
`ToPropertyKey` internally). So a computed key's `toString`/`Symbol.toPrimitive`
ran *after* the value expression, e.g. `{ [key]: (sideEffect(), 1) }`
produced the order `[value, key-toString]` instead of
`[key-toString, value]`.

**Fix:** emit `OP_to_propkey` for a computed key (`name == JS_ATOM_NULL`)
immediately after the `:`, before parsing the value. The later
`OP_define_array_el` re-converts an already-primitive key, so there is no
double `toString`. `src/quickjs/quickjs.c`, `js_parse_object_literal`.

Covers test262
`language/expressions/object/computed-property-name-topropertykey-before-value-evaluation.js`.

### 6. RegExp `v` flag carries full Unicode semantics

**Spec:** [`RegExp` `v` flag (`unicodeSets`)](https://tc39.es/ecma262/#sec-regexp-pattern-flags).
The `v` flag is a superset of `u`: it enables full Unicode semantics
(`\p{…}`/`\P{…}` property escapes, `\u{…}` code-point escapes, code-point
iteration over astral characters, Unicode case folding) **and** the
unicodeSets class extensions.

**Bug:** `libregexp.c` derived `is_unicode` solely from `LRE_FLAG_UNICODE`
(the `u` flag) and treated `is_unicode`/`unicode_sets` as mutually
exclusive. Every Unicode-semantic branch in the parser and matcher is
gated on `is_unicode`, so under `/…/v` property escapes silently matched
nothing, `\u{1F600}` failed, astral characters weren't iterated by code
point, and case folding fell back to ASCII. e.g. `/\p{ASCII}/v.test("a")`
returned `false` and `"a".match(/\p{L}/v)` returned `null`.

**Fix:** set `is_unicode` from `LRE_FLAG_UNICODE | LRE_FLAG_UNICODE_SETS`
in both the compiler (`lre_compile`) and the matcher (`lre_exec`);
`unicode_sets` still selects the `v`-only class syntax. Audit confirmed
nothing relied on the old mutual exclusivity, and the change only affects
`/v` regexps (previously broken) — `u` and plain regexps are byte-for-byte
unchanged. `src/quickjs/libregexp.c`.

Covers test262
`built-ins/RegExp/prototype/exec/regexp-builtin-exec-v-u-flag.js` and the
`String.prototype.{match,matchAll,replace,search}` `*-v-u-flag.js`
property-escape subtests.

Still open under `v`: the unicodeSets **set operations** (`[A&&B]`
intersection, `[A--B]` subtraction), nested classes `[[…][…]]`, string
disjunctions `\q{…}`, and properties-of-strings such as `\p{RGI_Emoji}`
(the `rgi-emoji-*` subtests) — these need the ClassSetExpression grammar,
which this change does not add.

### 7. AsyncFromSyncIterator closes the sync iterator on rejection

**Spec:** [`AsyncFromSyncIteratorContinuation`](https://tc39.es/ecma262/#sec-asyncfromsynciteratorcontinuation)
takes a `closeOnRejection` flag (true for `%AsyncFromSyncIteratorPrototype%.next`,
false for `return`/`throw`). When `closeOnRejection` is true and the
result is not `done`:
- step 6: if `PromiseResolve(%Promise%, value)` completes abruptly, the
  sync iterator is closed (`AsyncFromSyncIteratorClose`) before rejecting;
- step 11: the `onRejected` reaction passed to `PerformPromiseThen` closes
  the sync iterator (then rethrows) when the wrapped value promise rejects.

**Bug:** `js_async_from_sync_iterator_next` always passed `JS_UNDEFINED`
as the `onRejected` handler and went straight to `reject` when
`js_promise_resolve` threw, so the underlying sync iterator's `return()`
was never called for a rejected/abrupt value. `for await … of` over a
sync iterable whose values are rejected promises leaked the iterator
(no `finally`, no `return`).

**Fix:** add a captured `onRejected` closure
(`js_async_from_sync_iterator_close_on_reject`) that performs
`IteratorClose(syncIterator, ThrowCompletion(reason))` via
`JS_IteratorClose(…, /*is_exception_pending*/true)`, and wire it (and the
`PromiseResolve`-abrupt close) only for `GEN_MAGIC_NEXT` with `done` false.
`return`/`throw` and the `done`-true and `IteratorValue`-abrupt cases keep
the no-close behavior per spec. `src/quickjs/quickjs.c`.

Covers test262
`built-ins/AsyncFromSyncIteratorPrototype/{next,throw}/*-{rejected-promise-close,poisoned-wrapper}.js`.

### 8. `ResolveExport` treats the same re-exported namespace as unambiguous

**Spec:** [`ResolveExport`](https://tc39.es/ecma262/#sec-resolveexport).
When a name is found through several `export *` stars, the results are
ambiguous only if they denote different bindings: *different Module
Record* **or** *different BindingName*. A `export * as ns from "mod"`
binding's identity is `{ [[Module]]: mod, [[BindingName]]: namespace }` —
the *imported* module, not the re-exporting one — so the same namespace
re-exported through two different modules is a single binding.

**Bug:** `js_resolve_export1` compared the re-exporting module and the raw
entry `local_name`, so two modules each doing `export * as foo from
"./common.js"` (both re-exported by a third) were reported as
`ambiguous` even though they resolve to the same `common` namespace.

**Fix:** add `js_resolved_export_binding`, which maps a resolved entry to
its canonical `(Module, BindingName)` — for a `export * as ns` entry
(`local_name == JS_ATOM__star_`) that is the imported module plus the
`namespace` marker. The star-merge step compares those canonical
bindings. Genuinely distinct bindings (two local exports, or namespaces of
*different* modules sharing an export name) still resolve to `ambiguous`.
`src/quickjs/quickjs.c`, `js_resolve_export1`.

Covers test262
`language/module-code/ambiguous-export-bindings/namespace-unambiguous-if-*.js`
and `import-and-export-propagates-binding.js`.
