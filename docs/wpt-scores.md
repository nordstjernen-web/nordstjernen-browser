# WPT scores

Tracks Nordstjernen's results on the
[web-platform-tests](https://github.com/web-platform-tests/wpt)
suite over time: a fixed 15-area slice measured exactly (see
"Tracked slice" below), and a sampled estimate of the whole-suite
score browsers are compared by. See `docs/wpt.md` for how the
harness integration works. Scores update via `scripts/wpt-score.sh`
and `scripts/wpt-estimate.sh`, which edit the tables in this file in
place — review the diff and commit it together with the data files
they regenerate.

## History

| Date | Nordstjernen | WPT | Files ok | Subtests passing | Notes |
|------|--------------|-----|----------|------------------|-------|
| 2026-06-12 | 9526465 | 3be6ba111 | 181/696 (26%) | 6921/16067 (43%) | full |
| 2026-06-12 | e000f76 | 3be6ba111 | 181/696 (26%) | 6921/16067 (43%) | partial: dom/lists |
| 2026-06-12 | 07f95d7 | 3be6ba111 | 182/697 (26%) | 10017/19502 (51%) | full |
| 2026-06-12 | 5cd79e7 | d8a8414e5 | 182/696 (26%) | 6991/16067 (43%) | partial: html/webappapis/atob/base64.any.js |
| 2026-06-12 | c752e51 | 3be6ba111 | 187/697 (26%) | 11739/19502 (60%) | full |
| 2026-06-12 | 49de47c | 3be6ba111 | 187/697 (26%) | 12097/19502 (62%) | partial: dom/nodes dom/events html/webappapis/atob |
| 2026-06-12 | f0fb7b7 | 3be6ba111 | 188/697 (26%) | 12269/19502 (62%) | partial: dom/nodes |
| 2026-06-12 | ce53924 | 3be6ba111 | 189/696 (27%) | 15001/19492 (76%) | partial: url |
| 2026-06-12 | 2bba43f | 3be6ba111 | 189/696 (27%) | 15121/19492 (77%) | partial: dom/nodes |
| 2026-06-12 | 7a67688 | 3be6ba111 | 191/696 (27%) | 15757/19492 (80%) | partial: dom/nodes |
| 2026-06-12 | 7110c2f | 3be6ba111 | 191/696 (27%) | 15774/19525 (80%) | partial: dom/events |
| 2026-06-12 | 78da000 | d8a8414e5 | 195/696 (28%) | 15958/19502 (81%) | partial: html/dom/elements html/semantics/forms/the-form-element |
| 2026-06-12 | ffde346 | 3be6ba111 | 196/696 (28%) | 16080/19535 (82%) | partial: dom/lists dom/nodes |
| 2026-06-12 | faf548f | d8a8414e5 | 200/696 (28%) | 15986/19535 (81%) | partial: html/dom/elements |
| 2026-06-12 | d54d3b8 | d8a8414e5 | 199/696 (28%) | 16302/20746 (78%) | full |
| 2026-06-13 | fcfeaf1 | d8a8414e5 | 206/696 (29%) | 16227/20620 (78%) | full |
| 2026-06-13 | fcfeaf1 | d8a8414e5 | 206/696 (29%) | 16327/20746 (78%) | partial: html/dom/elements console html/webappapis/timers |
| 2026-06-13 | 7b9a177 | d8a8414e5 | 210/696 (30%) | 16258/20620 (78%) | full |
| 2026-06-13 | 2639ddc | d8a8414e5 | 210/696 (30%) | 17245/20620 (83%) | partial: url |
| 2026-06-13 | f9fb57c | d8a8414e5 | 212/696 (30%) | 17257/20620 (83%) | partial: url |
| 2026-06-13 | f2448ed | d8a8414e5 | 223/696 (32%) | 18420/21529 (85%) | partial: dom/nodes |
| 2026-06-13 | 726e248 | d8a8414e5 | 227/696 (32%) | 18551/21529 (86%) | partial: dom/nodes |
| 2026-06-13 | f30dab1 | d8a8414e5 | 227/696 (32%) | 18707/21654 (86%) | partial: html/dom/elements/the-innertext-and-outertext-properties |
| 2026-06-14 | 9fe9c25 | f01d00b69 | 276/696 (39%) | 60345/66800 (90%) | full |
| 2026-06-15 | d52628c | d8a8414e5 | 283/696 (40%) | 60603/66800 (90%) | partial: dom/traversal |
| 2026-06-15 | 3a754e8 | d8a8414e5 | 284/696 (40%) | 50415/55389 (91%) | full |
| 2026-06-16 | 5442142 | a72c94d4a | 286/696 (41%) | 50493/55389 (91%) | partial: html/dom/elements |
| 2026-06-16 | fec36c7 | a72c94d4a | 290/696 (41%) | 50646/55464 (91%) | partial: url |
| 2026-06-16 | 9531924 | a72c94d4a | 292/696 (41%) | 61026/66617 (91%) | partial: dom/ranges |

"Files ok" counts test files where the harness completed and every
subtest passed; "subtests passing" counts individual testharness.js
results. The Notes column records whether a row came from a full or
partial run — a partial row mixes revisions for the areas it did not
touch.

## Per-area results — 2026-06-16

Per-file detail for this run: `docs/wpt-runs/2026-06-16-9531924.tsv`.

| Area | Files ok | Subtests passing | Fail | Timeout | Notrun | Precondition failed |
|------|----------|------------------|------|---------|--------|---------------------|
| `dom/nodes` | 112/275 | 8552/10060 | 1453 | 44 | 11 | 0 |
| `dom/events` | 48/167 | 363/742 | 334 | 22 | 23 | 0 |
| `dom/traversal` | 12/17 | 1566/1602 | 36 | 0 | 0 | 0 |
| `dom/ranges` | 25/55 | 41826/44537 | 2711 | 0 | 0 | 0 |
| `dom/lists` | 5/5 | 189/189 | 0 | 0 | 0 | 0 |
| `dom/collections` | 2/10 | 30/53 | 23 | 0 | 0 | 0 |
| `url` | 15/32 | 7205/7474 | 268 | 1 | 0 | 0 |
| `console` | 5/12 | 23/29 | 6 | 0 | 0 | 0 |
| `hr-time` | 4/13 | 14/23 | 8 | 1 | 0 | 0 |
| `html/webappapis/atob` | 1/1 | 380/380 | 0 | 0 | 0 | 0 |
| `html/webappapis/timers` | 8/12 | 12/14 | 2 | 0 | 0 | 0 |
| `html/dom/elements` | 34/56 | 586/781 | 192 | 1 | 2 | 0 |
| `WebCryptoAPI/digest` | 1/5 | 116/535 | 419 | 0 | 0 | 0 |
| `xhr/formdata` | 14/18 | 70/80 | 10 | 0 | 0 | 0 |
| `html/semantics/forms/the-form-element` | 6/18 | 94/118 | 24 | 0 | 0 | 0 |
| **Total** | **292/696** | **61026/66617** | **5486** | **69** | **36** | **0** |

## ROI by area — 2026-06-16

Where score is cheapest to win, from the same data. Available
gain is the non-passing subtest count (sorted descending);
gain per affected file is its density — high values mean one
root cause likely flips many subtests. Harness-broken files
never report (usually one missing API hangs the page) and their
real gain is understated, since their unreported subtests count
zero. Near-ok files are at most two subtests away from a clean
file.

| Area | Available gain | Affected files | Gain/file | Harness-broken | Near-ok |
|------|----------------|----------------|-----------|----------------|---------|
| `dom/ranges` | 2711 | 30 | 90.4 | 0 | 6 |
| `dom/nodes` | 1508 | 163 | 9.3 | 25 | 89 |
| `WebCryptoAPI/digest` | 419 | 4 | 104.8 | 0 | 0 |
| `dom/events` | 379 | 119 | 3.2 | 72 | 29 |
| `url` | 269 | 17 | 15.8 | 3 | 3 |
| `html/dom/elements` | 195 | 22 | 8.9 | 2 | 14 |
| `dom/traversal` | 36 | 5 | 7.2 | 0 | 2 |
| `html/semantics/forms/the-form-element` | 24 | 12 | 2.0 | 2 | 8 |
| `dom/collections` | 23 | 8 | 2.9 | 0 | 3 |
| `xhr/formdata` | 10 | 4 | 2.5 | 0 | 3 |
| `hr-time` | 9 | 9 | 1.0 | 2 | 7 |
| `console` | 6 | 7 | 0.9 | 1 | 6 |
| `html/webappapis/timers` | 2 | 4 | 0.5 | 2 | 2 |
| `dom/lists` | 0 | 0 | - | 0 | 0 |
| `html/webappapis/atob` | 0 | 0 | - | 0 | 0 |

## Top 10 improvements — 2026-06-15

Root-cause clusters mined from `docs/wpt-runs/2026-06-15-3a754e8.tsv`,
ranked by expected subtest gain inside the tracked slice. Unlike the
tables above, this list is analysis, not arithmetic — the scripts do
not regenerate it. Refresh it (re-cluster the failing subtests)
whenever the scores move materially, and date the heading.

NOTE on layering: `NodeIterator`/`TreeWalker`/`Range`/`NodeFilter` and
much of the DOM are implemented in **`data/js/polyfills.js`**, which
OVERRIDES the C in `src/js.c`. Fix the polyfill, not the dead C path —
grep `polyfills.js` first.

| # | Improvement | Evidence | Est. gain |
|---|-------------|----------|-----------|
| 1 | ~~**Real `iframe.contentDocument` realm documents**~~ — **landed.** `contentDocument` is now backed by a real `NS_NODE_DOCUMENT` node parented under the iframe (the old `Object.create(document)` facade silently routed `appendChild`/`removeChild`/`firstChild` to the *main* document and never terminated `parentNode` chains at a Document). `createHTMLDocument`/`createDocument` were unified onto the same live document-node machinery (no more stale snapshot getters). `Document-characterSet-normalization-1/2` now pass fully (315+339), and `Range-surroundContents`/`insertNode`/`extractContents`/`cloneContents`/`deleteContents` jumped from harness-broken to majority-passing. Residual `dom/ranges` failures are now a *separate* root cause — deeper Range-algorithm bugs (e.g. `surroundContents` setup throwing `TypeError` on text-node `newParent` cases), not the realm-document gate | `Document-characterSet-normalization-{1,2}.html` (now 315/339 pass), `Range-surroundContents.html` (0→690/1840) | landed |
| 2 | XML/XHTML **document variants + namespaced node model** — `Document-createElementNS` (281) and `Document-createElement` (88) fail only their "in XML/XHTML document" cases; `Node-lookupNamespaceURI` (48), `lookupPrefix`, `isDefaultNamespace` are stubs; per-element/attr namespace not tracked | `Document-createElementNS.html`, `Node-lookupNamespaceURI.html` | ~450 |
| 3 | Unblock the harness-broken `dom/events` files (pages hang waiting on `test_driver`). **Partly landed:** a testdriver bridge in the WPT hook (`data/js/wpt-hook.js`) now implements `test_driver.click`, `send_keys`, and a pointer/key `action_sequence`, synthesising real mouse/pointer/keyboard events through the engine. Files that only needed a click/keypress flipped from TIMEOUT to passing (`pointer-event-document-move`, `focus-event-document-move`, `handler-count`, `label-default-action`, `legacy-pre-activation-behavior`, `preventDefault-during-activation-behavior`). **Remaining** is two larger engine features, each its own root cause: (a) the ~40-file `non-cancelable-when-passive/` cluster needs passive-listener tracking (today `addEventListener` parses `passive` then discards it) plus `TouchEvent`/`WheelEvent` synthesis with cancelability derived from passivity; (b) the `scrolling/*` cluster needs real scroll + `scrollend` infrastructure | `non-cancelable-when-passive/*` (40 files), `scrolling/scrollend-*` | ~379 |
| 4 | ~~**`innerText`/`outerText` rendered-text algorithm**~~ — **largely landed.** The getter now flushes layout before reading (so dynamically-set `white-space`/`visibility`/`text-transform` are seen — `dynamic-getter.html` went 4/7→7/7), collects rendered text with a spec-style *required-line-break-count* model (`<p>` yields blank lines, `<br>` at end of block is kept, leading/trailing block breaks are dropped without eating preserved `white-space:pre` spaces), honours `visibility:hidden`/`collapse`, treats `display:contents` as boxless and flex/grid items as blockified, skips replaced/non-rendered subtrees (`textarea`/`iframe`/`audio`/`video`/`canvas`/`img`/`object`/`embed`/SVG/MathML), renders `<option>` and (label-less) `<optgroup>` as blocks, and applies `text-transform`. `getter.html` 159→232. **Remaining** is layout-geometry-dependent: tab-separated table cells, `::first-line`/`::first-letter` pseudo styling, shadow-DOM exclusion, whitespace at inline-block boundaries, and the `?white-space=` variant files the local runner cannot expand | `getter.html` (159→232), `dynamic-getter.html` (4/7→7/7) | landed (~80) |
| 5 | **URL long tail** — setter edge cases, constructor parsing, IDNA. **Partly landed:** the JS→C marshalling for URL/`<a>`/`<area>` component setters used `JS_ToCString`+`strlen`, which truncated the value at the first embedded U+0000; setters now pass the real byte length (`JS_ToCStringLen` → `ns_url_set_component_len`) so NULs are percent-encoded (`%00`) per the WHATWG percent-encode sets instead of cutting the value short. `url-setters-stripping` 217→260, `urlencoded-parser` 66→105, `url-setters-a-area` +28, `url-setters`/`url-constructor`/`percent-encoding` smaller gains (~153 total). **Remaining**, each its own root cause: lexbor host-setter port parsing (`host='example.com:invalid'` should set hostname and stop the port parser), opaque-path erasure for non-special schemes, trailing-space handling when clearing `search`/`hash` on opaque paths, legacy query-encoding (`big5`/`euc-kr`/…) percent-encoding, and `IdnaTestV2` | `url-setters-stripping.any.html` (217→260), `urlencoded-parser.any.html` (66→105) | landed (~153) |
| 6 | **Exotic platform objects** — `querySelectorAll` returns a real `Array` (should be a `NodeList`: `[object NodeList]`, no tamperable `length`); `HTMLCollection` wrongly has `values`/`entries`/`forEach` and an own `length`. Make both proper WebIDL legacy-platform objects | `dom/collections/*` (23), `dom/nodes/NodeList-static-length-getter-tampered-*` ×6 | ~30 |
| 7 | **`document.createRange()` bound to the wrong document** — `NdRange`'s constructor hard-coded the main-realm `document` as the initial start/end container, so `foreignDoc.createRange()`/`xmlDoc.createRange()` (and the ranges cloned from them) reported the *main* document as their container instead of the document the method was called on. `createRange` now passes its receiver document through `__ndCreateRange(ownerDoc)` to the constructor. `Range-cloneRange` 40→62 (all pass). | `Range-cloneRange.html` (40→62) | landed (~22) |
| 8 | **iframe classic-script globals not visible via the in-frame `window`** — the single biggest remaining cluster (`Range-surroundContents` 690/1840, plus `Range-extractContents`/`cloneContents`/`deleteContents`, all on the same shared `dom/common.js` harness). **Root cause isolated** (this was previously mis-described as a "transient `textContent` no-op"): an iframe's own classic scripts bind their top-level `var`/`function` declarations to the per-frame global var environment, but those bindings are **not reachable through the `window` object used *inside* the frame** — they are reachable as bare identifiers and via the parent's `iframe.contentWindow`, and the same code in the top-level page binds `var x` → `window.x` correctly, so the defect is specific to the iframe scope/proxy in `ns_iframe_scope_bootstrap` + `ns_js_run_iframe_scripts` (`src/js.c`). In the harness, `setupRangeTests` (in `common.js`) rebuilds `paras`/`testDiv` through one binding while `run()`'s `eval("[paras[0]…]")` and the parent's `iframe.contentWindow.testRange` read a divergent one, so after the first `(range,node)` cell `paras[0].firstChild` is null and range setup throws `TypeError`. Minimal repro: external `<script>` with `var x=123` in an iframe → inside the frame `x===123` but `window.x===undefined`; in the top page both are `123`. Fix is an iframe-realm change (make the in-frame `window`/WindowProxy resolve the frame's global var bindings, or bind frame-script vars onto the frame window) — deep and high-risk since this scaffolding is what makes the 690 pass; needs design + broad re-test, not a blind patch. | `Range-surroundContents.html` (690/1840); repro: iframe external-script `var x` vs `window.x` | ~1150 (blocked, root-caused) |

Not listed: `WebCryptoAPI/digest` — 419 of its failures are
`tentative` SHA-3/cSHAKE/K12/TurboSHAKE tests (`kangarootwelve` 148,
`turboshake` 135, `cshake` 88) for algorithms not yet standardized;
only the failures in `digest.https.any.html` are mandatory surface.
## Whole-suite score

Full browsers are compared by total passing subtests across the
entire WPT suite — a scale where Chrome scores roughly 6,000,000.
Running all of WPT through Nordstjernen is impractical (days of
wall-clock at the per-test timeout), so this score is estimated by
sampling: a deterministic random sample of test files drawn from
every browser-runnable testharness.js test in the checkout, run
through the headless mode, with the mean passing-subtest count per
file extrapolated to the whole population. The 95% interval is a
bootstrap over the sample.

```sh
scripts/wpt-estimate.sh --wpt-root=~/wpt
```

Caveats, so the number is read honestly: only browser-runnable
testharness tests are counted (worker and service-worker variants,
reftests, crashtests, and wdspec tests are excluded — Chrome's
headline number includes those, so the "% of Chrome" column slightly
flatters Nordstjernen); tests whose harness never reports (hung page,
missing API) contribute zero even though they contain subtests; and
the subtest-per-file distribution is heavy-tailed, hence the wide
interval. Treat the trend, not the point value, as the signal.

| Date | Nordstjernen | WPT | Sample | Est. passing subtests (95% CI) | % of Chrome (~6M) |
|------|--------------|-----|--------|--------------------------------|-------------------|
| 2026-06-12 | 9526465 | 3be6ba111 | 250 of 29259 | ~44,000 (22,000 – 72,000) | ~0.7% |
| 2026-06-13 | fcfeaf1 | d8a8414e5 | 250 of 29259 | ~255,256 (21,184 – 706,429) | ~4.3% |

Runtime estimate: a 696-file tracked-slice run on this Windows/MSYS2
machine took 36m25s, or about 3.1 seconds per file with the 15000 ms
browser timeout. Extrapolated to the 29259 browser-runnable
testharness files counted by the estimator, a testharness-only
all-suite run would take about 25 to 26 hours. The timeout-heavy
upper bound is about 5 days at the browser timeout alone, or about
8.5 days with the script's 25 second outer timeout. Including worker
and service-worker variants, reftests, crashtests, and wdspec would
push a literal all-WPT run toward several days, and some of those
harness types are not wired into Nordstjernen's current headless
runner yet.

## Running the slice

Each run uses the headless runner with default settings (15000 ms
per-test timeout) against a current WPT checkout:

```sh
scripts/dev.sh build
scripts/wpt-score.sh --wpt-root=~/wpt
```

Partial runs rerun just the given areas (or sub-paths of them) after
an engine change, without paying for the full slice:

```sh
scripts/wpt-score.sh --wpt-root=~/wpt dom/nodes url
scripts/wpt-score.sh --wpt-root=~/wpt dom/events/Event-constructors.any.js
```

Fresh results replace the matching rows in `docs/wpt-subtests.tsv`;
everything not rerun carries over from the previous run, and the
regenerated tables always cover the whole slice.

## Data files

`docs/wpt-runs/DATE-REV.tsv` is the per-test-file snapshot of each
run — one row per test file with the harness status and subtest
pass/fail/timeout/notrun/precondition_failed counts. Diffing two of
them shows which test files regressed or improved between revisions.

`docs/wpt-subtests.tsv` is the canonical per-subtest state — one row
per individual subtest (test file, subtest name, status), plus a
`<harness>` row per file. Each run overwrites it, so its git history
is the subtest-level time series: `git log -p --
docs/wpt-subtests.tsv` shows exactly which named subtests flipped
between any two runs. Failure messages are not committed (noisy and
large); reproduce them with `scripts/wpt-run.sh --results=FILE`.

## Improving the score

The loop, drivable end-to-end by a Claude session:

1. Take the highest unclaimed entry from the Top 10 improvements
   list (falling back to the ROI table when the list is stale), or
   find the individual test files with the most failing subtests:

   ```sh
   sort -t$'\t' -k5 -rn docs/wpt-runs/2026-06-12-9526465.tsv | head -20
   ```

2. List the failing subtests by name, and the files whose harness
   never completes (usually a missing API hanging the page):

   ```sh
   awk -F'\t' '$2 != "<harness>" && $3 != "PASS"' docs/wpt-subtests.tsv | grep '^/dom/ranges/'
   awk -F'\t' '$2 == "<harness>" && $3 != "OK"' docs/wpt-subtests.tsv
   ```

3. Reproduce one test with full failure messages:

   ```sh
   ./builddir/src/gtk/nordstjernen --wpt http://web-platform.test:8000/dom/ranges/Range-attributes.html
   ```

4. Fix the engine, rebuild, and rerun just the affected area:

   ```sh
   scripts/wpt-score.sh --wpt-root=~/wpt dom/ranges
   ```

5. Commit the engine change together with the regenerated doc and
   data files. The `docs/wpt-subtests.tsv` diff is the proof of which
   subtests flipped.

## Tracked slice

`dom/nodes`, `dom/events`, `dom/traversal`, `dom/ranges`,
`dom/lists`, `dom/collections`, `url`, `console`, `hr-time`,
`html/webappapis/atob`, `html/webappapis/timers`,
`html/dom/elements`, `WebCryptoAPI/digest`, `xhr/formdata`,
`html/semantics/forms/the-form-element`

The slice is fixed so results stay comparable between runs; WPT
itself moves, so small drifts in totals between WPT revisions are
expected. If the slice ever changes, start a new history table.
