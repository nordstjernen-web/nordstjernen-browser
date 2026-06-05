# Benchmarking Nordstjernen on Speedometer 3.1

The official [Speedometer 3.1](https://browserbench.org/Speedometer3.1/)
harness loads every workload inside an `<iframe>` and drives it through
`frame.contentDocument` / `frame.contentWindow` after `frame.onload`.
Nordstjernen now supports this: setting an iframe's `src`/`srcdoc`
fetches and parses the document, splices it under the iframe node so it
styles, lays out, and runs its own scripts, marks the iframe
`data-nd-frame-loaded` so the UA stylesheet renders it, and dispatches
`load`. `HTMLIFrameElement` exposes `contentDocument` (documentElement /
body / head / getElementById / querySelector / querySelectorAll /
defaultView) and a scoped `contentWindow` facade with frame-local
`location` and `history`. Individual official suite runs produce scores.

Multi-suite aggregate runs now drive to a geomean score as well — the
runner reloads its one test iframe per suite/iteration and the engine
keeps up. (Earlier these appeared to hang; that was the per-reload
document-index and orphan/pinned-wrapper growth turning each reload
O(n^2), since fixed, plus the headless settle running ~2x the
`--settle-ms` budget, so a too-short timeout killed the run before it
finished.) Example aggregate over the eleven light TodoMVC frameworks
(ES5, ES6-Webpack, WebComponents, React, React-Redux, Backbone, Vue,
jQuery, Preact, Svelte, Lit), `iterationCount=1`:

```sh
./builddir/src/nordstjernen \
  'http://localhost:8124/index.html?suite=TodoMVC-JavaScript-ES5,TodoMVC-JavaScript-ES6-Webpack,TodoMVC-WebComponents,TodoMVC-React,TodoMVC-React-Redux,TodoMVC-Backbone,TodoMVC-Vue,TodoMVC-jQuery,TodoMVC-Preact,TodoMVC-Svelte,TodoMVC-Lit&startAutomatically=true&iterationCount=1'
```

`window` is still the one shared global, so cross-suite state (framework
globals, window listeners) can still leak between suites in a single
aggregate run; treat the aggregate as indicative, and prefer per-suite or
fresh-process runs when comparing the engine over time.

```sh
# Serve the harness and run it headless to a score:
git clone --depth 1 --branch release/3.1 \
    https://github.com/WebKit/Speedometer.git /tmp/spdm
( cd /tmp/spdm && python3 -m http.server 8124 & )
./builddir/src/nordstjernen --headless --dump=text --settle-ms=60000 \
    'http://localhost:8124/index.html?suite=TodoMVC-JavaScript-ES5&startAutomatically=true&iterationCount=1'
```

For per-suite engine tracking we still also drive each Speedometer 3.1
*workload* directly via `scripts/speedometer-bench.sh`, which fetches
Speedometer's `release/3.1` sources at runtime, generates a page driver
from them, replays Speedometer's own per-suite interaction steps against
the top-level document, and times each suite. Nothing third-party is
vendored into this repository.

## Running

```sh
scripts/dev.sh build
scripts/speedometer-bench.sh            # every loadable TodoMVC suite
scripts/speedometer-bench.sh complex    # only paths containing "complex"
```

Each suite is run `ND_ITERS` times (default 3) and the median total is
reported. Per phase the driver records `sync` time (the interaction loop) plus
`async` time (a forced layout flush on the next frame), matching the real
runner's `measurementMethod = "raf"` measurement. `domNodes` is the live
element count at the end of the run.

## What runs

Suites that drive end to end and produce a score in the **official iframe
harness** (run individually, `?suite=<name>&startAutomatically=true`):

- All seven light-DOM TodoMVC frameworks and their `-Complex-DOM` variants:
  vanilla JS (ES5, ES6-Webpack), React, Angular, Vue, Preact, Svelte.
- **React-Redux** and **WebComponents** — after the window-event-handler and
  custom-element fixes (`src/js.c`); WebComponents builds its list with `new
  TodoItem()` into shadow roots, which now yields real element instances.
- The non-TodoMVC **Charts-chartjs**, **Charts-observable-plot**,
  **Editor-CodeMirror**, **Editor-TipTap**, and **NewsSite-Nuxt** suites.
- **Lit** - after custom-element construction stopped reusing the upgrading
  element for ordinary `EventTarget` subclasses created inside custom element
  constructors.
- **jQuery** and **Backbone** — after giving iframe scripts their own scoped
  `document` (`src/js.c`), running an iframe's classic scripts in one shared
  scope (so Backbone's cross-file `var app` is shared), and serializing
  `<script>` innerHTML unescaped (`src/dom.c`) so Handlebars/Underscore
  templates compile to real markup.

Each iframe's classic `<script>`s run concatenated inside one
`(function(document){...})` wrapper, with `document` bound to a facade scoped
to the iframe's subtree (a per-call current-document swap); module scripts
keep their own scope. This also dropped Next.js's head-manager errors to
zero. `window` is still the shared global, so the remaining gaps are:

Still imperfect:

- **NewsSite-Next** — past the head errors, but Next.js hydration doesn't
  drive to completion. **React-Stockcharts-SVG** currently renders its
  Stockcharts fit wrapper placeholder but never creates the SVG crosshair
  cursor, and **Perf-Dashboard** still reaches the harness UI but returns no
  score. Multi-suite aggregate runs are also polluted by stale window
  `load`/`hashchange` listeners and framework globals left by previous suites.

Current fresh-process official-suite snapshot on Windows (2026-06-03):
17 of 20 suites score, geomean **2.4340**. This is not the official
single-page aggregate score; it is the comparable per-suite score while the
shared global limitation remains.

## Snapshot (release build, Windows 11, GTK 4.22, 2026-06-05)

Median total ms, lower is faster. The "prior" column is the 2026-06-03
Linux snapshot after the first cascade pass; "total" is after the
2026-06-05 render-pipeline work (preorder rank map for absolute
positioning, fused pseudo-element cascade gathering, intrinsic-width
caching, offscreen paint culling).

| Suite                         |  prior | total |   add | complete | delete | DOM   |
|-------------------------------|-------:|------:|------:|---------:|-------:|------:|
| TodoMVC-Angular               |    230 |   522 |   493 |        9 |      5 |   641 |
| TodoMVC-JavaScript-ES5        |    228 |   660 |   575 |       77 |     47 |    43 |
| TodoMVC-React                 |    352 |   673 |   504 |       13 |    103 |    24 |
| TodoMVC-Svelte                |    361 |   669 |   522 |      109 |     60 |    20 |
| TodoMVC-Preact                |   2610 |  2808 |  1049 |     1300 |    562 |    60 |
| TodoMVC-jQuery                |      — |  1665 |   636 |      534 |    303 |    41 |
| TodoMVC-Backbone              |      — |   604 |   530 |       30 |     24 |    50 |
| TodoMVC-React-Complex-DOM     |   8681 |  1851 |  1704 |       14 |     97 |  6632 |
| TodoMVC-Angular-Complex-DOM   |   8052 |  1597 |  1583 |        9 |      5 |  7249 |
| TodoMVC-JavaScript-ES5-Complex|   8607 |  1675 |  1498 |       90 |     60 |  6651 |
| TodoMVC-Svelte-Complex-DOM    |   8663 |  1572 |  1422 |      103 |     47 |  6628 |
| TodoMVC-Preact-Complex-DOM    |  10353 |  3750 |  1988 |     1115 |    647 |  6678 |
| TodoMVC-jQuery-Complex-DOM    |      — | 10229 |  9174 |      746 |    371 |  6649 |
| TodoMVC-Backbone-Complex-DOM  |      — |  5802 |  5779 |       36 |     34 |  6658 |

The Complex-DOM suites dropped ~5x with the 2026-06-05 pass (the small-DOM
columns are not comparable across the two machines). The remaining outliers
are jQuery-Complex and Backbone-Complex, whose add phases do per-item
selector queries over the whole document.

The numbers are not comparable to an official Speedometer score — this driver
has no warmup suite and no geomean aggregation. They are a self-consistent
per-workload measurement for tracking the engine over time.

## Snapshot (release build, Linux, GTK 4.14, 2026-06-05) — custom-property cascade

`scripts/speedometer-bench.sh`, median total ms over 3 iterations, lower is
faster. "prior" is the Linux baseline before this pass; "total" is after
sharing inherited custom-property tables by reference instead of deep-copying
them per element (`build_vars_for_element` in `src/css.c`).

| Suite                          | prior | total |
|--------------------------------|------:|------:|
| TodoMVC-Backbone-Complex-DOM   |  5914 |  1392 |
| TodoMVC-jQuery-Complex-DOM     |  9646 |  2833 |
| TodoMVC-Preact-Complex-DOM     |  3581 |  2649 |
| TodoMVC-Vue-Complex-DOM        |  2041 |  1081 |
| TodoMVC-Lit-Complex-DOM        |  2263 |  1121 |
| TodoMVC-Angular-Complex-DOM    |  1232 |   433 |
| TodoMVC-React-Complex-DOM      |  1459 |   518 |
| TodoMVC-Svelte-Complex-DOM     |  1468 |   550 |
| TodoMVC-JavaScript-ES5-Complex |  1503 |   548 |
| TodoMVC-ES6-Webpack-Complex    |  1560 |   546 |

The Speedometer "complex" CSS declares ~1600 custom properties on `:root` that
inherit to every element. `build_vars_for_element` used to materialise that
full table for each element that declared even one local `--var`
(`g_strdup` of every key and value), so a single full-document re-cascade did
tens of millions of string copies. A per-element profile attributed ~80% of
the whole cascade to this one step. The inherited table is immutable once
built, so it is now shared by reference: each element holds a small
copy-on-write layer (`nd_var_map`) of just its own overrides chained to the
parent's map, and `var()` lookups walk the chain. The `@property`-registered
path (where non-inheriting/initial semantics force a flat table) is unchanged.
This cut the var-heavy complex suites 2–4x with no change to the light suites,
and the same speedup carries over to the Speedometer `main` (post-3.1) beta
workloads, which share the complex CSS.

## Profiling

The `*-Complex-DOM` variants cost ~30-40x their small-DOM twins, and the cost
is almost entirely the `Adding100Items` phase. The ES5-Complex suite uses no
framework, so its time is pure engine. The shape is a **full-document restyle
on every DOM mutation**: each of the 100 inserts (each forced layout flush)
re-cascades the whole tree, so the cost scales as O(items x nodes x rules).
The small-DOM suites stay fast because their trees are tiny; the complex-DOM
suites carry ~7000 static nodes that are re-styled 100 times over.

A 150-sample gdb profile of the add phase originally attributed ~41% to glibc
allocator churn (`_int_malloc` / `free` / `malloc_consolidate`) and ~30% to
GLib hashing behind selector matching, under `cascade_walk` →
`gather_matches_impl` (`src/css.c`). Two changes cut the allocator churn:

- **Skip empty pseudo-element passes.** `cascade_walk` ran eight pseudo-element
  gather passes (`::before`/`::after`/`::first-letter`/…) per element, each
  allocating four arrays, even when no stylesheet styled that pseudo-element.
  Each stylesheet now records a bitmask of the pseudo-elements its selectors
  target (built with the rule index), and passes for absent pseudo-elements
  are skipped entirely.
- **Reuse cascade scratch arrays.** The four per-element match arrays were
  `g_array_new`/`g_array_free`d for every node on every relayout; they are now
  cleared-and-reused scratch buffers.
- **Skip the per-element variable map** when no custom properties are in scope
  (`build_vars_for_element` returns NULL instead of allocating an empty hash
  table for every node).
- **Build the box-lookup index lazily.** `getBoundingClientRect` no longer
  builds a whole-document box index to satisfy a single query per reflow.

Together these drop the complex-DOM suites ~20-29% (see the table).

A fresh 60-sample profile after these changes shows the remaining cost is the
**inherent full-document re-cascade**: leaf time is dominated by the glibc
allocator (`_int_malloc`/`free`/`malloc_consolidate`, ~45%) and `g_str_hash` /
`g_hash_table_lookup` from selector-index matching, with `nd_style_free` high
in the inclusive frames — i.e. allocating and freeing ~7000 computed styles
(and their `nd_css_value`s) on each of the 100 reflows. The two levers that
remain, both larger and correctness-sensitive, are:

1. **Incremental restyle** — re-cascade only the mutated subtree (plus the
   siblings/ancestors that structural selectors depend on) instead of the whole
   document, turning the add phase from O(items x nodes) toward O(items).
2. **Refcount `nd_css_value`** — `cascade_for` deep-copies every matched and
   inherited value into each element's style and `nd_style_free` frees them
   all; sharing immutable values by refcount would remove most of the per-node
   alloc/free churn. This needs an audit that no site mutates a value in place.

To reproduce the profile:

```sh
meson setup builddir-prof --buildtype=debugoptimized -Db_lto=false
meson compile -C builddir-prof
# in one shell, start a long-settle run of a complex-DOM suite, then:
scripts/sample-profile.sh <pid> 150
```

## querySelector key-index fast path (2026-06-05)

A sampling profile of the `TodoMVC-*-Complex-DOM` add phases (now that the
cascade allocator churn is tamed by value refcounting and shared inherited
custom-property tables) showed the dominant remaining engine cost shifting
from the cascade to `nd_walk_first_match` — the `querySelector` /
`querySelectorAll` slow path, which walked the whole ~6600-node document and
tested every element. The framework suites issue per-item selector queries
over the top-level document on each of the 100 inserts.

`nd_query_selector_simple` already short-circuits bare `#id`, `.class`, and
`tag` queries through the document id/class/tag indices. The slow path now
applies the same idea to compound and descendant selectors via a **rightmost
key bucket**: for a single-selector query whose subject (rightmost) compound
carries a positive id/class/tag, candidates are gathered from that document
index and verified with the full `nd_css_selector_matches` (which already does
right-to-left combinator matching), instead of walking the subtree. The
candidate set is provably identical to the walk's — the document indices skip
`<template>` content with the same guard the walk uses — so only ordering
shares the existing index-order caveat the simple fast path already accepts.

The bucket path is gated to top-level (`root == document`) queries: for an
element-scoped query on a small subtree whose rightmost key is common
document-wide, walking the small subtree is cheaper than scanning a whole
document-wide bucket, so element-rooted queries keep the walk. The cheap
`#id` single-node lookup still applies for any root.

Median-of-3 over the loadable Speedometer 3.1 TodoMVC workloads
(`scripts/speedometer-bench.sh`, release build, Linux), lower is faster:

| Aggregate                    | before | after | delta |
|------------------------------|-------:|------:|------:|
| Sum of all suite totals      |  26135 | 25232 | -3.5% |
| Sum of Complex-DOM totals    |  17932 | 17279 | -3.6% |

The query-bound suites move most: jQuery-Complex-DOM -7.5%, Preact-Complex-DOM
-7.6%, Preact -6.5%, React -6.2%, Backbone -3.8%. The same engine path serves
the Speedometer `main` (4.0-alpha, "speedometer next") workloads, which share
the TodoMVC suites; `scripts/speedometer4-bench.sh` drives those directly
(it fetches the `main` branch and generates a page driver from
`default-tests.mjs` the same way the 3.1 harness does from `tests.mjs`).
