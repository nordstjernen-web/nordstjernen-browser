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

"Files ok" counts test files where the harness completed and every
subtest passed; "subtests passing" counts individual testharness.js
results. The Notes column records whether a row came from a full or
partial run — a partial row mixes revisions for the areas it did not
touch.

## Per-area results — 2026-06-14

Per-file detail for this run: `docs/wpt-runs/2026-06-14-9fe9c25.tsv`.

| Area | Files ok | Subtests passing | Fail | Timeout | Notrun | Precondition failed |
|------|----------|------------------|------|---------|--------|---------------------|
| `dom/nodes` | 107/275 | 9122/10060 | 883 | 44 | 11 | 0 |
| `dom/events` | 48/167 | 363/744 | 334 | 23 | 24 | 0 |
| `dom/traversal` | 5/17 | 1308/1602 | 294 | 0 | 0 | 0 |
| `dom/ranges` | 24/55 | 40882/44537 | 3655 | 0 | 0 | 0 |
| `dom/lists` | 5/5 | 189/189 | 0 | 0 | 0 | 0 |
| `dom/collections` | 1/10 | 23/53 | 30 | 0 | 0 | 0 |
| `url` | 11/32 | 7071/7474 | 402 | 1 | 0 | 0 |
| `console` | 7/12 | 51/56 | 5 | 0 | 0 | 0 |
| `hr-time` | 5/13 | 31/51 | 19 | 1 | 0 | 0 |
| `html/webappapis/atob` | 1/1 | 380/380 | 0 | 0 | 0 | 0 |
| `html/webappapis/timers` | 9/12 | 13/14 | 1 | 0 | 0 | 0 |
| `html/dom/elements` | 31/56 | 631/907 | 272 | 2 | 2 | 0 |
| `WebCryptoAPI/digest` | 1/5 | 116/535 | 419 | 0 | 0 | 0 |
| `xhr/formdata` | 14/18 | 70/80 | 10 | 0 | 0 | 0 |
| `html/semantics/forms/the-form-element` | 7/18 | 95/118 | 23 | 0 | 0 | 0 |
| **Total** | **276/696** | **60345/66800** | **6347** | **71** | **37** | **0** |

## ROI by area — 2026-06-14

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
| `dom/ranges` | 3655 | 31 | 117.9 | 0 | 6 |
| `dom/nodes` | 938 | 168 | 5.6 | 25 | 91 |
| `WebCryptoAPI/digest` | 419 | 4 | 104.8 | 0 | 0 |
| `url` | 403 | 21 | 19.2 | 3 | 5 |
| `dom/events` | 381 | 119 | 3.2 | 72 | 29 |
| `dom/traversal` | 294 | 12 | 24.5 | 0 | 6 |
| `html/dom/elements` | 276 | 25 | 11.0 | 2 | 16 |
| `dom/collections` | 30 | 9 | 3.3 | 0 | 3 |
| `html/semantics/forms/the-form-element` | 23 | 11 | 2.1 | 2 | 7 |
| `hr-time` | 20 | 8 | 2.5 | 2 | 5 |
| `xhr/formdata` | 10 | 4 | 2.5 | 0 | 3 |
| `console` | 5 | 5 | 1.0 | 0 | 5 |
| `html/webappapis/timers` | 1 | 3 | 0.3 | 2 | 1 |
| `dom/lists` | 0 | 0 | - | 0 | 0 |
| `html/webappapis/atob` | 0 | 0 | - | 0 | 0 |

## Top 10 improvements — 2026-06-12

Root-cause clusters mined from `docs/wpt-subtests.tsv`, ranked by
expected subtest gain inside the tracked slice. Unlike the tables
above, this list is analysis, not arithmetic — the scripts do not
regenerate it. Refresh it (re-cluster the failing subtests) whenever
the scores move materially, and date the heading.

| # | Improvement | Evidence | Est. gain |
|---|-------------|----------|-----------|
| 1 | Unblock the 71 harness-broken `dom/events` files (pages hang before reporting — missing event/`click()` infrastructure; one fix likely unblocks many files) | `Event-dispatch-click.html`, `EventListener-invoke-legacy.html`, … | 352 visible, real gain larger |
| 2 | Realm-document identity: `iframe.contentDocument` and `element.ownerDocument` must be the same object per document, XHTML iframes need realm docs | remaining `Document-createElementNS.html` 289, `Document-createElement.html` 88 | ~380 |
| 3 | Range API correctness; 25 of 55 `dom/ranges` files are harness-broken | `Range-cloneContents.html`, `Range-collapse.html`, … 201 visible | 201 visible, real gain larger |
| 4 | Attr-node APIs: `createAttribute(NS)`, `get/setAttributeNode`, `removeAttributeNode`, InUseAttributeError, namespace-aware node lookup | `dom/nodes/attributes.html` 54, `Document-createAttribute.html` 36 | ~90 |
| 5 | Form collection named/indexed access and `requestSubmit` edge cases | `form-elements-*`, `form-nameditem.html`, `form-requestsubmit.html` | ~23 |
| 6 | URL long tail: `idlharness` interface checks, `urlencoded-parser` edge cases, opaque-path percent-encoding of `%00` and space | `url/idlharness.any.html` 65, `urlencoded-parser.any.html` 42 | ~150 |



Not listed: `WebCryptoAPI/digest` — 419 of its 451 failures are
`tentative` SHA-3/cSHAKE/K12/TurboSHAKE tests for algorithms not yet
standardized; only the 32 failures in `digest.https.any.html` are
mandatory surface.

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
