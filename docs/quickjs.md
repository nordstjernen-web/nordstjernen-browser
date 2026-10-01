# QuickJS or quickjs-ng

The QuickJS binding (`-Djs_engine=quickjs`, the default) runs on one of two
QuickJS engines, picked at configure time with the `quickjs` option:

```sh
meson setup builddir                          # default: in-tree quickjs-ng fork
meson setup builddir -Dquickjs=quickjs-ng     # the same, explicitly
meson setup builddir -Dquickjs=quickjs        # Fabrice Bellard's original QuickJS
```

An existing build directory switches with
`meson configure builddir -Dquickjs=quickjs`. The option is ignored by
`-Djs_engine=v8`.

| | `quickjs-ng` (default) | `quickjs` |
|---|---|---|
| Source | `src/quickjs/`, an in-tree fork of [quickjs-ng](https://github.com/quickjs-ng/quickjs) | [bellard/quickjs](https://github.com/bellard/quickjs), the original engine |
| How it gets into the build | always in the tree | fetched at configure time by `subprojects/quickjs.wrap`, never vendored |
| Version | quickjs-ng 0.16.2 plus the browser hooks | release 2026-06-04, pinned by commit |
| About page | `QuickJS 0.16.2` | `QuickJS 2026-06-04` |
| CI | every workflow | `.github/workflows/quickjs.yml` |

Both builds carry the same Web API surface. The whole binding — `src/js.c`
and its satellites — is written against the quickjs-ng API; the original
engine gets that API through a small adapter instead of a second binding.

## How the original engine is built

`subprojects/quickjs.wrap` pins the upstream repository at the commit that
made the 2026-06-04 release. Upstream ships only a Makefile, so the wrap's
`patch_directory` overlays `subprojects/packagefiles/quickjs/meson.build`,
which compiles the library objects the Makefile builds (`quickjs.c`,
`dtoa.c`, `libregexp.c`, `libunicode.c`, `cutils.c`) with the same defines
(`_GNU_SOURCE`, `CONFIG_VERSION`, `-fwrapv`) into a static library with
hidden symbols. Nothing is installed. A build with
`--wrap-mode=nodownload` needs the checkout already in
`subprojects/quickjs/`.

To move to a newer release, point `revision` in the wrap at the new release
commit, delete `subprojects/quickjs/`, reconfigure, and rerun the checks
below.

## The adapter: `src/ns_quickjs.h`

Engine code includes `"ns_quickjs.h"`, never `<quickjs.h>`. With the fork it
is a plain include. With the original engine (`NS_QUICKJS_ORIGINAL`, set by
`meson.build`) it supplies the quickjs-ng API the binding uses:

- **Different signatures.** `JS_IsArray`, `JS_IsError` and `JS_IsBigInt`
  take no context in quickjs-ng; `JS_NewArrayBuffer` takes a realloc-style
  callback and a maximum length; `JS_NewContext` is wrapped so the adapter
  can learn the engine's class IDs. These are macros over functions in
  `src/ns_quickjs.c`. The promise-rejection tracker's `is_handled` argument
  is `ns_js_bool`, which is `bool` on quickjs-ng and `JS_BOOL` on the
  original.
- **quickjs-ng additions.** `JS_IsArrayBuffer`, `JS_IsDataView` and
  `JS_GetTypedArrayType` compare class IDs learned once, from objects made
  in the first context. `JS_ToObject`, `JS_NewStringUTF16`,
  `JS_ThrowDOMException`, `JS_IsStrictEqual`, `JS_AtomIsArrayIndex` and
  `JS_GetVersion` are rebuilt from public calls. `JS_EvalThis2` pads the
  source with the script's line and column offset, because the original
  `JS_EvalThis` takes no position, so errors still point at the right line
  of the HTML document.
- **Fork-only hooks.** The fork's own additions have no equivalent and fall
  back, as described below.

The bytecode cache keys entries by source text only, and the two engines'
bytecode is not interchangeable, so the original engine keeps its cache in
`~/.cache/nordstjernen/jsbc/quickjs-<version>/`.

## Known differences on the original engine

The fork carries browser hooks and compatibility changes that the original
engine lacks. On `-Dquickjs=quickjs` these differ from the default build:

- **Named access across frames.** `frame.contentWindow.someId` looks the id
  up in the calling document rather than the frame's. Frames share their
  parent's `Window.prototype` chain, and only the fork's receiver-aware
  property hook can tell which window is being read. Named access within a
  page, `window.someId` or a bare `someId`, is unaffected.
- **Calling realm.** `JS_GetCallerRealm` reports the realm of the native
  function being called rather than the realm of its caller, and
  `JS_GetFunctionRealm` reports the current realm rather than the
  function's own. Across frames, this can change which document a
  cross-realm `createElement` or custom element is tagged with, and puts
  `window.event` on the top-level window while a listener from a frame
  runs.
- **WebAssembly memory.** A view made on an imported
  `WebAssembly.Memory`'s `buffer` before the module is instantiated is
  detached by instantiation instead of being repointed at the instance's
  memory. Re-reading `memory.buffer` gives a working buffer, as it does
  after `grow()` on both engines.
- **Language behaviour.** The fork's compatibility changes are absent:
  `RegExp.$1`–`$9`, `Function.prototype.caller`, `Error.captureStackTrace`,
  `Array.fromAsync` and `using` declarations are missing, and `error.stack`
  has no leading `Name: message` line. `Array.prototype.sort` never calls
  the comparator for two identical values, so `jQuery.uniqueSort` (and the
  jQuery methods built on it) keeps duplicate elements.
- **Speed.** `JS_AtomIsArrayIndex` goes through a string, so indexed access
  on host objects (collections, `frames[i]`) is slower.

## Checking a change

Build both configurations with no warnings, GCC and Clang:

```sh
meson setup builddir && meson compile -C builddir
meson setup builddir-quickjs -Dquickjs=quickjs && meson compile -C builddir-quickjs
NS_BIN=$PWD/builddir-quickjs/src/gtk/nordstjernen ./scripts/dev.sh smoke
```

A binding change that calls a quickjs-ng function the original engine lacks
fails to compile in `builddir-quickjs`; add the function to
`src/ns_quickjs.c` rather than an `#ifdef` at the call site.
