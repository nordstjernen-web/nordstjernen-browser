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
`en_US.UTF-8`), `MAX_COMPONENTS` (the most components inventoried per
page in each browser, see below; default 3000). The three steps can
also be run on their own; each has `--help`, and the capture scripts
take `--runs` and `--visual-runs` (both default 1) and the inventory
cap, a positive number, as `--max-components=N`.

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
and an inventory of the visible components with their geometry, font
size, weight, family and colours, in two parts:

- `components`: up to 500 headings, links, buttons, inputs, images,
  media and landmarks in the first three screens, as in earlier
  versions. The first-screen scores use only these, so they stay
  comparable with earlier runs.
- `moreComponents`: the rest of the page. The same kinds further down or
  past those 500, and the elements that hold text themselves: `sup`,
  `sub`, `td`, `th`, and `span` and `div` with text of their own.
  Components lying entirely left or right of the viewport are left out.

`moreComponents` stops once the two parts hold `MAX_COMPONENTS`
components together, and on a longer page the rest of it, in document
order, is left out. `components` is collected in full regardless, so a
capture can hold a few more than `MAX_COMPONENTS`, and with a cap below
500 the first part alone may exceed it. The two browsers can stop at
different points of the page, as one may lay out components the other
hides. The whole-page score then covers the page only up to where the
first of the two inventories stopped, the last component of a capped
inventory that the other one holds too, and leaves out the components
past that point instead of counting them as missing. The larger
inventory costs probe time and disk: on frozen copies of four large
pages (Booking, The New York Times, Stack Overflow and a long Wikipedia
article) the probe takes 4 to 14 ms longer in Chrome and 50 to 360 ms
longer in Nordstjernen than for `components` alone, and `probe.json`
grows from 25–145 KB to 0.4–0.9 MB. The Wikipedia article has about 7400
components, of which the default inventory keeps 3000.

Each component also carries its DOM path, the chain of `tag:n` steps
below the document element down to it, such as
`body>div:2>main>p:3>sup>a`, where `n` counts the elements of that tag
among its siblings and is left out for the first. The probe builds the
path while it walks the tree, so both browsers compute it the same way.

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
- *Whole-page placement*: the share of Chrome's identifiable components
  in the whole inventory, `components` and `moreComponents` together,
  that Nordstjernen places with IoU ≥ 0.5 in their surroundings. Before
  the overlap is taken, the Nordstjernen component is moved back by the
  offset between the two browsers of the nearest component above it
  under the same parent that has a partner, or, when there is none, of
  its nearest enclosing component in the inventory that has one. A
  component that overlaps Chrome's at the same place on the page counts
  as placed as well. So a difference in height, which moves everything
  below it, counts once, where it arises, and not again for every
  component further down, and a container whose edge moved while its
  content stayed does not misplace that content. The report also gives
  the share of components at the same place on the page. Both are
  reported next to the first-screen share and are not part of visual
  parity.
- *First paint ÷ FCP*: Nordstjernen's first painted frame over Chrome's
  first contentful paint. Below 1.00× Nordstjernen paints first.
- *Images loaded ÷ load*: Nordstjernen's first frame with its images in
  over Chrome's `load` event.
- *Main-thread CPU ÷ Chrome*: CPU time of Nordstjernen's main thread over
  the settled load, over Chrome's main-thread task time for the same
  window. The time Nordstjernen spends compressing its screenshot dumps
  to PNG (`encode_ms`) is left out, as Chrome's screenshots are not
  taken on its main thread. So is the time the probe itself takes
  (`probeMs` in `probe.json`), since Chrome's figure is read before the
  probe runs. Only the serialisation of the probe's result stays in,
  which even with a full inventory does not show above the difference
  between two loads. Captures made before the probe recorded its time
  count it in, and a report that compares them with newer ones names
  them.
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
median SSIM and both medians of components placed (first screen and
whole page), which then cover the stable sites only, and get a mean
parity row of their own; the summary gives the number of sites each mean
covers, and the other figures still cover every compared site. When two
or more builds are compared, the per-site tables show the newest build's
parity minus the oldest's, and a difference no larger than the site's
noise, the largest of Chrome's spread and the two builds' spreads, is
marked as noise, unless the newest build failed in more or fewer runs
than the oldest, which is shown instead; the summary lists the sites
that moved by more or whose failed runs changed. A capture with one
visual run per browser has no spread, and its report is the same as
before visual runs were repeated. A Chrome capture taken with one visual
run, which `run.sh` (`CHROME=auto`) and a resumed `ab.sh` keep when they
find it, has no spread either, so the builds' spreads alone decide
stability and noise for that site until Chrome is captured again
(`CHROME=1`, or a new `OUT` for `ab.sh`).

To place Chrome's components, `compare.py` pairs each one with at most
one Nordstjernen component, and each Nordstjernen component serves at
most one Chrome component. Components with the same DOM path and the
same key, that is tag, text (digits ignored) and id, are paired first.
The rest are paired by key: pairs that overlap more go first, and what
is left of each key is paired in the order the probe listed them. This
finds a component whose path changed because an element before it
exists in only one browser, such as a placeholder image or a banner a
script inserted. Last, components still unpaired that share a DOM path
are paired by path alone: mostly the same element with another text,
such as a headline that changed between the two loads or text decoded
in another charset. A Chrome component without a partner counts as not
found. The first-screen scores pair `components` with `components`, the
whole-page score the two whole inventories. Captures made before
components had paths are paired by key alone, and there is no
whole-page score unless both captures have `moreComponents`, so a
Chrome capture kept from an earlier version needs `CHROME=1` once.

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
usually enough to point at the engine feature at fault. Each listed
component shows the end of its DOM path (hover it for the whole path),
the path of the Nordstjernen component it was paired with when that one
sits elsewhere in the tree, and whether it was paired by path, by key
or by path alone. Above the list, the report counts, for the first
screen and for the whole page, the components paired each way and those
left unpaired in each browser. It names each capture whose inventory
reached `MAX_COMPONENTS`, and counts the components of each browser that
the whole-page score leaves out past the point where the other's
inventory stopped. With repeated visual runs, the screenshots, the
heatmap, the components and the style mismatches are those of Chrome's
reference run and each build's median run, while the filmstrip and the
full pages come from the first run; *Visual runs* adds every run's
screenshot and score, and is open for unstable sites.

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
Nordstjernen's main-thread CPU in this run still includes the time the
probe took, which later versions leave out.

Nordstjernen paints before Chrome's first contentful paint on 13 sites
and uses less than a third of Chrome's memory, but still spends about
three times Chrome's main-thread CPU and scores 71 of 100 for looking
like it: the cascade and layout on large, script-built pages are where
the next work is.

## Frozen snapshots

Live sites change from one load to the next, so the scores above move by a
few points on their own, and a fix that corrects one kind of element can
vanish in them. `snapshot.py` measures layout on a fixed input instead: it
saves a script-free copy of each site once, then compares the box of every
element on those copies in Nordstjernen and in Chrome. It needs Python 3,
curl and an installed Chrome or Chromium, and runs on Linux and macOS.

```sh
scripts/sitebench/snapshot.py freeze                       # every site in sites.tsv
scripts/sitebench/snapshot.py freeze --only=wikipedia-article,bbc
scripts/sitebench/snapshot.py score                        # builddir's build against Chrome
scripts/sitebench/snapshot.py ab /path/to/old/nordstjernen builddir/src/gtk/nordstjernen
```

- `freeze` loads the live page in headless Chrome over the DevTools
  protocol, with the flags, user agent, language and time zone that
  `chrome-capture.js` gives Chrome. Once the page has loaded, the network
  has gone quiet and `--settle-ms` has passed, it writes the style rules
  that scripts added or changed through the CSSOM, as CSS-in-JS libraries
  do, into their style elements and the state scripts gave form controls,
  such as a checked radio button, into their attributes, and takes the DOM
  with its doctype, so a page in quirks mode stays in quirks mode. It drops scripts, iframes and
  event handler attributes, inlines the style sheets with their imports,
  saves the images and fonts they use next to the page in
  `sitebench-out/snapshots/<id>/`, removes `loading="lazy"` so that both
  browsers load every image, and stops CSS animations and transitions, so
  that every measurement sees the same page. A file that cannot be
  fetched, or that comes back as an error or bot-check page, counts as
  missing; it and any video, audio or embedded object point to a missing
  local file. The copies hold the sites' own content: they stay out of the
  repository, and are shared only privately. `freeze` exits with status 1
  when it could freeze none of the sites.
- `score` serves the copies from a local web server, records the box of
  every element under `body` in Chrome and in the build, with any request
  to another host refused, and counts the elements that match: width,
  height and offset in the parent all within 2 px of Chrome's, for the
  element at the same place in the DOM. Chrome's boxes are kept in
  `sitebench-out/snapshot-chrome/`, per Chrome version, viewport and
  locale, and measured again when a copy changes. It exits with status 1
  when the build crashes, times out or prints no elements on a site, or,
  with `--min-share=P`, when fewer than P percent of the elements match.
- `ab` renders the old build twice and the new one once. The difference
  between the two renders of the old build is the noise, and a site counts
  as changed when its change is larger than half its noise plus 2
  elements. It counts the elements each site won and lost, lists examples
  of the lost ones for every site that got worse, and exits with status 1
  when the new build matches fewer elements than the old one beyond the
  noise, when it crashes, times out or prints no elements on a site, or,
  with `--max-site-loss=N`, when it loses more than N of the elements the
  old build matched on one site, beyond that site's noise. A site the old
  build fails on is left out and listed.

Both commands exit with status 2 on usage errors and when they fail to
run at all.

Both reports (`sitebench-out/report/snapshots.md` and `snapshots.json`)
also rank where the build fails most. Every failing element is traced to
the failure it most likely follows from: a wrong width to the parent's
wrong width, a wrong height to the child that grew, a wrong offset to the
earlier sibling that failed, a missing element to its missing parent. The
sources with the most failing elements behind them are listed by site,
kind and element. A weight counts the failing elements traced to a
source: an estimate of what fixing it would win, since a fix can also
correct elements traced to another source.

Nordstjernen runs in a throwaway home directory under `--locale` (default
`en_US.UTF-8`, or `NS_LOCALE`), and Chrome in the same language;
`--viewport` (default `VIEWPORT` or `1280x800`), `--jobs`, `--settle-ms`
and `--chrome` (or `CHROME_BIN`) set the rest. Element boxes depend on the
installed fonts, so compare numbers only between runs on the same machine,
against the same Chrome.

## Adding sites

`sites.tsv` is `id<TAB>category<TAB>url`. Keep ids short and stable; they
name the output directories. `SITES=FILE` runs another list in the same
format, such as saved copies of pages served from a local server, which
unlike the live sites stay the same from run to run.
