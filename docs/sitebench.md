# Site benchmark: Nordstjernen vs Chrome

`scripts/sitebench/` loads the most visited sites on the web in Chrome and
in Nordstjernen, and measures how close Nordstjernen comes to Chrome in
what the page looks like and in how fast and how cheaply it gets there.
Chrome is the reference: the goal is to look the same and to be faster.

It is a measuring tool, not a test suite: live sites change between runs,
so read the numbers as a trend over many sites, and look at the
screenshots.

## Running it

Requirements: a Nordstjernen build (`scripts/dev.sh build`), Python 3 with
Pillow and numpy, and Node.js with Playwright for the Chrome side.

```sh
cd scripts/sitebench && npm install && npx playwright install chromium && cd -
scripts/sitebench/run.sh                         # all sites in sites.tsv
scripts/sitebench/run.sh --only=google,youtube   # a few sites
scripts/sitebench/run.sh --category=news         # one category
```

The report is written to `sitebench-out/report/index.html` (with
`summary.md` and `summary.json` next to it). Chrome is captured once and
reused (`CHROME=1` re-captures it); each run captures Nordstjernen under
`LABEL` (default `nordstjernen`).

Comparing two Nordstjernen builds, say before and after a change:

```sh
scripts/sitebench/ab.sh /path/to/old/builddir/src/gtk/nordstjernen \
                        builddir/src/gtk/nordstjernen
```

`ab.sh` loads each site in Chrome and then in both builds before moving
on, so all three see the live page within a few minutes of each other;
capturing one build after the other an hour apart lets the sites' own
changes (a new top story, a different ad) show up as differences between
the builds. It writes to `sitebench-out/ab/` and resumes where it
stopped when run again. With `run.sh` the same comparison is

```sh
NS_BIN=/path/to/old/builddir/src/gtk/nordstjernen LABEL=before scripts/sitebench/run.sh
LABEL=after LABELS=before,after scripts/sitebench/run.sh
```

Settings come from the environment: `RUNS` (cold loads per site and
browser, medians are reported; default 3), `VIEWPORT` (default
`1280x800`), `OUT` (default `sitebench-out`). The three steps can also be
run on their own; each has `--help`.

To use an installed Google Chrome instead of Playwright's Chromium, run
`node scripts/sitebench/chrome-capture.js --channel=chrome` (or
`--executable=PATH`).

Running as root (a container) sets `NS_ALLOW_ROOT=1` for Nordstjernen. A
CA bundle named by `CURL_CA_BUNDLE` or `SSL_CERT_FILE` is readable from
inside the sandbox, so the benchmark also works behind a TLS-inspecting
proxy.

## What is measured

Both browsers load each site cold (fresh profile, empty cache) in a
1280×800 viewport. Both evaluate the same probe, `probe.js`, after the
page settles: document size, DOM size, text length, navigation timing,
and an inventory of up to 500 visible components in the first three
screens (headings, links, buttons, inputs, images, media, landmarks) with
their geometry, font size, weight, family and colours.

**Chrome** (`chrome-capture.js`, Playwright + DevTools protocol):

- viewport and full-page screenshots, and a screencast filmstrip from
  which the report derives the Speed Index;
- TTFB, first paint, FCP, LCP, CLS, total blocking time, DOMContentLoaded
  and load;
- main-thread task, script, style and layout time (`Performance.getMetrics`),
  JS heap, the RSS of all Chrome processes;
- requests, bytes per resource type, failed requests, console errors.

**Nordstjernen** (`ns-capture.py`, headless mode):

- `RUNS` cold loads with `--settle-ms=0 --timing`: the time to the first
  painted frame (the viewport painted after the first layout, before
  images arrive, the counterpart of Chrome's first contentful paint), the
  time until that frame's images are in, each phase (fetch, parse,
  cascade, parser-blocking scripts, layout, image decode, paint), network
  wait, process CPU time and peak RSS;
- one settled load (`--settle-ms=2000 --time-ms=1000`) for the
  screenshots, the probe, main-thread CPU over the whole load, and the JS
  errors reported on stderr. As in the browser window, images are fetched
  while the page settles, as each new layout asks for them, and their
  `load` events fire as they arrive.

**Scores** (`compare.py`):

- *Visual parity* (0–100) weighs viewport SSIM (30%), colour histogram
  intersection (15%), overlap of the content regions (20%), the share of
  Chrome's identifiable first-screen components that Nordstjernen places
  with IoU ≥ 0.5 (25%) and text coverage (10%).
- *First paint ÷ FCP*: Nordstjernen's first painted frame over Chrome's
  first contentful paint. Below 1.00× Nordstjernen paints first.
- *Images loaded ÷ load*: Nordstjernen's first frame with its images in
  over Chrome's `load` event.
- *Main-thread CPU ÷ Chrome*: CPU time of Nordstjernen's main thread over
  the settled load, over Chrome's main-thread task time for the same
  window. The time Nordstjernen spends compressing its screenshot dumps
  to PNG (`encode_ms`) is left out, as Chrome's screenshots are not
  taken on its main thread.
- *Peak memory ÷ Chrome*: Nordstjernen's peak RSS over the RSS of all
  Chrome processes after load.

Sites that answer headless Chrome with a bot challenge or an error page
instead of their content (detected from the page title and first text,
or a challenge redirect such as `?js_challenge=` on a near-empty page)
are marked in the report and left out of the averages, since there is
nothing to compare against. A site that shows one of the compared
Nordstjernen builds a challenge is left out too, so that every column
averages the same sites.

The report lists, per site, the screenshots side by side with a
difference heatmap, Chrome's filmstrip, both full pages, the
worst-placed components and the computed-style mismatches, which is
usually enough to point at the engine feature at fault.

## Adding sites

`sites.tsv` is `id<TAB>category<TAB>url`. Keep ids short and stable; they
name the output directories.
