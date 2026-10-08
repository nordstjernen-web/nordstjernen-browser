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

The report header names each build by the first twelve hex digits of
its SHA-256, after `git describe` of the Nordstjernen checkout it sits
in or, for a build copied out of its checkout, the version compiled into
it: `nordstjernen 1.0.29-114-ge2d59b3d sha256:478226592b3f` or
`nordstjernen 1.0.30-dev sha256:478226592b3f`. The hash tells two builds
of one checkout apart, since `git describe` describes the checkout's
current HEAD, which need not be what the binary was built from.

Settings come from the environment: `RUNS` (cold loads per site and
browser, medians are reported; default 3), `VIEWPORT` (default
`1280x800`), `OUT` (default `sitebench-out`), `SITES` (the site list;
default `sites.tsv`), `NS_LOCALE` (the locale Nordstjernen runs under,
see below; default `en_US.UTF-8`). The three steps can also be run on
their own; each has `--help`.

To use an installed Google Chrome instead of Playwright's Chromium, set
`CHROME_CHANNEL=chrome` (or `CHROME_EXECUTABLE=PATH` for a given Chrome
or Chromium binary); `run.sh` and `ab.sh` pass it to `chrome-capture.js`
as `--channel` (`--executable`), and `npx playwright install chromium`
is then not needed.

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
  JS heap, the RSS of all Chrome processes (read from `/proc` on Linux and
  from `ps` on macOS; elsewhere it is not measured and the memory ratio
  stays empty);
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

On Linux and macOS both browsers run in US English, whatever the host's
locale: Chrome with the `en-US` locale and Nordstjernen under
`en_US.UTF-8`. `ns-capture.py` sets `LANG` and `LC_ALL` to it and
removes `LANGUAGE`, `FC_LANG` and `NS_PANGO_LANGUAGE`, which would take
precedence. The locale decides Nordstjernen's `Accept-Language` header,
`navigator.languages` and `Intl` default, and which name fontconfig gives
each font family: under `tr_TR.UTF-8` it lists the system font as
"Sistem Fontu", so `system-ui` text falls back to Helvetica. On Windows,
Nordstjernen takes `Accept-Language` from the Windows user locale
instead. `NS_LOCALE` (or `--locale`) picks another locale; the report
header shows each build's locale next to Chrome's, and "not recorded"
for captures made before the locale was. Both browsers also run in UTC,
Chrome through its context's time zone and Nordstjernen with `TZ=UTC`,
so dates and times a page prints read the same in both.

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

## Results

The run that measured the engine work on the branch that added this
benchmark (8 October 2026, `ab.sh`, 3 cold loads per site and browser,
4-core VM, software rendering). *before* is `main` at d81f5de with the
benchmark's own `--timing` reporting added, *after* is the branch head.
Chrome is headless Chromium 141.

| | before | after |
|---|---|---|
| Sites loaded | 42 of 44 | 44 of 44 |
| Mean visual parity | 64.3 | 70.7 |
| Median first paint (ms) | 1590 | 1357 |
| First paint ÷ Chrome FCP (geomean) | 1.47× | 1.29× |
| Sites painting before Chrome FCP | 10 | 13 |
| Images loaded ÷ Chrome load event (geomean) | 1.05× | 0.88× |
| Main-thread CPU ÷ Chrome (geomean) | 4.48× | 3.56× |
| Main-thread CPU, sum over the 42 sites both builds loaded | 472 s | 260 s |
| Peak memory ÷ Chrome (geomean) | 0.28× | 0.29× |
| JS errors (all sites) | 147 | 136 |

Chrome spent 90 s of main-thread time on the same 42 sites, with a
median FCP of 1048 ms and a median load event at 1936 ms. Sixteen sites
are left out: fifteen showed headless Chrome a bot challenge or an
error page, and TikTok showed one to Nordstjernen. The before build
aborted with heap corruption on claude.com and Discord (an inline VP9
video overran its frame buffer), which counts as parity 0 in its mean;
over the 42 sites both builds loaded, parity went from 67.3 to 71.0.

Nordstjernen paints before Chrome's first contentful paint on 13 sites
and uses less than a third of Chrome's memory, but still spends about
three times Chrome's main-thread CPU and scores 71 of 100 for looking
like it: the cascade and layout on large, script-built pages are where
the next work is.

## Adding sites

`sites.tsv` is `id<TAB>category<TAB>url`. Keep ids short and stable; they
name the output directories. `SITES=FILE` runs another list in the same
format, such as saved copies of pages served from a local server, which
unlike the live sites stay the same from run to run.
