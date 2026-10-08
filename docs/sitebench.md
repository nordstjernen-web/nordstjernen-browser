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
browser, medians are reported; default 3), `VISUAL_RUNS` (settled loads
per site and browser whose screenshot and probe are kept and scored;
default 3), `VIEWPORT` (default `1280x800`), `OUT` (default
`sitebench-out`), `SITES` (the site list; default `sites.tsv`),
`NS_LOCALE` (the locale Nordstjernen runs under, see below; default
`en_US.UTF-8`). The three steps can also be run on their own; each has
`--help`, and the capture scripts take `--runs` and `--visual-runs`
(both default 1).

The comparison can also gate a change. `MIN_PARITY=P` fails the run
when the newest label's mean visual parity, which covers the stable
sites (see *Scores*), is below P. `MAX_DROP=D` fails it when the newest
label's mean visual parity is more than D below the oldest label's,
the two means taken over the stable sites both labels captured; when a
stable site's parity is more than D below the oldest label's beyond the
site's noise (with one visual run the noise is 0); or when the newest
label failed in more of a site's visual runs than the oldest, on any
site both captured. When the two labels were captured on different
sites, `compare.py` names the sites only one of them has. `run.sh` and
`ab.sh` check both values, and `run.sh` that `MAX_DROP` has two labels
to compare, before capturing anything, pass them to `compare.py` as
`--min-parity` and `--max-drop` and exit with its status: 1 when a
check fails, with one line per failed check; 2 on a usage error, when
there are no captures, when a label a check needs has no site to
compare, or when a capture or the comparison itself fails; 0 otherwise.
Without thresholds, a run that writes its report exits 0 as before.

```sh
MAX_DROP=1 scripts/sitebench/ab.sh /path/to/old/builddir/src/gtk/nordstjernen \
                                   builddir/src/gtk/nordstjernen
```

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

The screenshots and the probe come from Chrome's first `VISUAL_RUNS`
cold loads: the first load also takes the full page and the filmstrip,
the others only the viewport. When `VISUAL_RUNS` is above `RUNS`,
Chrome loads the site that many times, and its medians still cover only
the first `RUNS` loads.

**Nordstjernen** (`ns-capture.py`, headless mode):

- `RUNS` cold loads with `--settle-ms=0 --timing`: the time to the first
  painted frame (the viewport painted after the first layout, before
  images arrive, the counterpart of Chrome's first contentful paint), the
  time until that frame's images are in, each phase (fetch, parse,
  cascade, parser-blocking scripts, layout, image decode, paint), network
  wait, process CPU time and peak RSS;
- `VISUAL_RUNS` settled loads (`--settle-ms=2000 --time-ms=1000`) for
  the screenshots and the probe; the first also gives main-thread CPU
  over the whole load and the JS errors reported on stderr. As in the
  browser window, images are fetched while the page settles, as each new
  layout asks for them, and their `load` events fire as they arrive.

Each browser keeps its first visual run where a single run has always
gone (`viewport.png`, `full.png` and `probe.json` in the site's
directory). Every further run writes its `viewport.png` and `probe.json`,
and Nordstjernen's `stderr.log`, to `visual-2/`, `visual-3/` and so on
in the same directory, without a full-page screenshot; `metrics.json`
records the count under `settings.visualRuns`. Each run past the first
costs Nordstjernen one more settled load per site and build, 5 to 20 s
on an Apple M2 with a median of 7.6 s (10 to 39 s, median 15 s, for the
two extra runs of the default), and adds 0.1 to 0.6 MB per site and
browser. Chrome takes its visual runs from loads it makes anyway for
`RUNS`, so with the defaults its extra cost is one viewport screenshot
per run.

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

Pages change between loads (a consent banner, a rotating top story), so
one load can mislead; the visual runs measure how much. Every
Nordstjernen run is scored against one Chrome run, the *reference*: the
Chrome run whose screenshot and probe score highest, on average, as the
reference for Chrome's other runs (the first on a tie). A site's parity
is the median over a build's runs, and the build's *median run* is the
run with that parity (the lower of the two middle runs for an even
count). Its *spread* is the highest minus the lowest parity of the runs
that loaded; Chrome's spread is 100 minus the lowest score of its other
runs against the reference. A build's *failed* run, one that ended with
a load error or without a screenshot, counts in the median (as 0 when it
has no screenshot) but not in the spread, which measures how much the
rendered page changes between loads; the site is marked ⚠ for that
build and, as after a failed single load, left out of its sites loaded
and its timing aggregates. A site is *unstable* when Chrome's spread or
the spread of any compared build is above 2 points. Unstable sites are
marked in both reports and left out of the mean visual parity, the
median SSIM and the median components placed, which then cover the
stable sites only, and get a mean parity row of their own; the summary
gives the number of sites each mean covers, and the other figures still
cover every compared site. When two or more builds are compared, the
per-site tables show the newest build's parity minus the oldest's, and
a difference no larger than the site's noise, the largest of Chrome's
spread and the two builds' spreads, is marked as noise, unless the
newest build failed in more or fewer runs than the oldest, which is
shown instead; the summary lists the sites that moved by more or whose
failed runs changed. A capture with one visual run per browser has no
spread, and its report is the same as before visual runs were repeated.
A Chrome capture taken with one visual run, which `run.sh`
(`CHROME=auto`) and a resumed `ab.sh` keep when they find it, has no
spread either, so the builds' spreads alone decide stability and noise
for that site until Chrome is captured again (`CHROME=1`, or a new
`OUT` for `ab.sh`).

Sites that answer headless Chrome with a bot challenge or an error page
instead of their content (detected from the page title and first text,
or a challenge redirect such as `?js_challenge=` on a near-empty page)
are marked in the report and left out of the averages, since there is
nothing to compare against. A site that shows one of the compared
Nordstjernen builds a challenge is left out too, so that every column
averages the same sites. Challenges are judged on Chrome's reference
run and on each build's median run.

The report lists, per site, the screenshots side by side with a
difference heatmap, Chrome's filmstrip, both full pages, the
worst-placed components and the computed-style mismatches, which is
usually enough to point at the engine feature at fault. With repeated
visual runs, the screenshots, the heatmap, the components and the style
mismatches are those of Chrome's reference run and each build's median
run, while the filmstrip and the full pages come from the first run;
*Visual runs* adds every run's screenshot and score, and is open for
unstable sites.

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
