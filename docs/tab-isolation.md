# Tab isolation and renderer boundaries

This note records the current isolation shape in Nordstjernen and the
next useful implementation steps. The current direction is:

- keep using per-tab workers to remove GTK-free work from the UI thread;
- build snapshot boundaries so the main thread stops walking mutable tab
  state while painting;
- treat real security isolation as a later process-per-site renderer
  boundary, not as something threads can provide.

## Current state

Nordstjernen is one browser process with N tabs. Each tab is an
`ns_window` (`src/window.h`) owned by the GTK application. Most renderer
state is already per tab:

- parsed DOM (`parsed_doc`), layout tree, computed-style table, scroll,
  focus, history cursor, CSP, image/video cache, and animation state;
- one QuickJS runtime and context per tab, created by `ns_js_new()` in
  `src/js.c`;
- classic Dedicated Workers created by that tab get their own QuickJS
  runtime, GLib main context, and OS thread, with message handoff back
  to the owning tab;
- one serial `ns_tab_worker` per tab, created by
  `ns_browser_construct_tab()` in `src/main.c`;
- one `GtkDrawingArea` per tab, painted by `ns_draw_render()`.

The old process-global active JavaScript pointer has been replaced by a
thread-local `GPrivate` slot. JavaScript still runs on the main thread,
but the runtime ownership model is now compatible with future tab-owned
thread work as long as each QuickJS runtime is only entered from one
thread at a time.

Networking is already asynchronous. `ns_net_fetch_async()` runs blocking
curl work on a GTask worker pool, with global and per-origin throttles,
then returns results to the main loop. Shared browser state is guarded
where it is currently shared:

| Subsystem | Global state | Lock / owner |
| --- | --- | --- |
| Disk byte cache | `bcache.c` `g_mem` | `g_lock` |
| HTTP cache | `cache.c` SQLite handle | `g_cache_mutex` |
| History | `history.c` SQLite handle | `g_history_mutex` |
| Cookies / HSTS / curl share | `net.c` globals | `g_hsts_lock`, curl share locks |
| Bytecode cache | `bcache.c` `g_mem` | `g_lock` |
| Font cache | `font.c` entries | main thread / Pango |
| Config | `config.c` `g_cfg` | browser process |

Cookies are partitioned by registrable site, and same-origin, CORS, CSP,
mixed-content, and SRI checks exist. In the current single address
space, these are policy boundaries, not memory-safety boundaries.

## Per-tab worker status

The per-tab worker is deliberately serial. That keeps publication order
simple, avoids intra-tab worker races, and still lets different tabs
prepare pages and resources in parallel.

Current worker-owned jobs:

| Work item | State | Main-thread publication |
| --- | --- | --- |
| Main page body decode | done | generation-checked completion |
| Main page HTML parse | done | parsed tree is accepted only for the live tab and fetch generation |
| Top-level external CSS parse | done | stylesheet publication, cascade invalidation, and layout invalidation remain on the main loop |
| Still-image decode | mostly done | worker returns owned pixels; main loop creates `ns_texture` / `GdkTexture` |
| Video poster decode | mostly done | same pixel-buffer handoff for still images |
| Animated GIF decode | mostly done | worker returns owned frame pixels; main loop creates animation textures |
| GDK-Pixbuf / librsvg image fallback | mostly done | worker decodes to pixels, with temporary texture download only as a compatibility fallback |
| `@import` expansion | main thread | expansion inspects live tab policy: CSP, mixed content, stylesheet grouping |
| JavaScript parse / execution | main thread | QuickJS is per tab, but DOM bindings mutate live tab state |
| Dedicated Worker JavaScript | worker thread | classic worker scripts run in a separate QuickJS runtime; message payloads cross through QuickJS object serialization without bytecode or SharedArrayBuffer flags |
| Layout | main thread | depends heavily on Pango measurement |
| Paint | main thread | Cairo / Pango draw directly into the tab drawing area |

Fetch generation checks are now used across page and stylesheet
publication, including Stop/cancel paths. Old network or worker results
should not overwrite a newer navigation once `fetch_gen` has advanced.

## What is isolated today

The current architecture gives useful fault containment inside the
program structure:

- tab DOMs, style state, JS heaps, scroll/focus state, and resource
  caches are separate objects;
- CPU-heavy page preparation, CSS parsing, and some image decoding can
  run away from the GTK main loop;
- shared caches and network state are behind explicit locks;
- process-wide seccomp and Landlock reduce the whole process's system
  call and file-system reach after startup;
- existing `fork()` / `socketpair()` patterns in the watchdog and media
  broker show that process machinery is available in the tree.

This is not a renderer sandbox. Any memory-safety bug in the browser
process can still read or corrupt another tab's DOM, JS heap, cookies,
or cache state. Threads improve responsiveness and reduce accidental
cross-tab coupling, but they do not provide a security boundary.

## Main blockers

Display-list snapshotting is the next architectural boundary. Today the
paint path walks live layout and DOM-related structures on the main
thread. Before layout or script can safely move further off-thread, the
renderer needs an immutable paint snapshot that the main thread can
consume without touching mutable tab state.

Pango is the layout blocker. Text measurement and shaping are spread
through `src/layout.c`, `src/paint.c`, `src/font.c`, selection, export,
and canvas text. The safest near-term strategy is to keep Pango-owned
objects on the main thread and introduce a batched text-measurement
service. Per-thread FT2-backed `PangoContext`s may be viable later, but
they are a cross-platform risk and should be proven behind a small
experiment before layout depends on them.

JavaScript is per-tab but not off-thread-ready. QuickJS runtimes are
separate, but DOM bindings currently mutate the live tree that layout,
paint, events, and selection also inspect. Script movement should wait
until DOM mutation, layout invalidation, and paint snapshots have a
clear ownership protocol.

Dedicated Workers are isolated from the DOM but not from the browser
address space. They are useful for page-authored background JavaScript
and for proving per-runtime thread ownership, but they do not protect
tabs from native memory-corruption bugs. Keep worker APIs conservative:
same-origin classic scripts first, no SharedArrayBuffer, no bytecode
deserialization, no transfer lists until ownership rules are explicit,
and no worker `fetch` until network callbacks are guaranteed to resolve
on the worker context through teardown.

Process isolation remains the security boundary. A future renderer
process should not read cookies, history, cache databases, downloads, or
general disk state directly. Those capabilities need browser-process
services and a small IPC protocol before renderer subprocesses can be a
meaningful sandbox.

The existing opt-in WebGL path is an isolation complication because it
binds GL state to GTK-local process state. Do not expand it while the
renderer boundary is unsettled; keep it out of any first renderer split.

## Suggested next steps

1. Finish the worker resource handoff.

   Still images and animated GIF frame sequences now return owned pixels
   from the worker. The remaining cleanup is to remove the rare
   temporary texture-download compatibility fallback and keep
   display-backend objects entirely on the main thread.

2. Tame stylesheet imports.

   Keep CSP, mixed-content, SRI, and stylesheet-group decisions on the
   main thread, but move any pure `@import` parse work that does not
   require live tab policy behind the worker boundary. Preserve the same
   tab id and fetch generation checks used by top-level CSS.

3. Add a display-list snapshot.

   Introduce a compact immutable display-list type with commands for
   backgrounds, borders, text runs, images, replaced elements, clips, and
   scroll offsets. First generate and paint it on the main thread. The
   first win is not threading; it is making paint consume a stable
   snapshot instead of live DOM/layout state.

4. Put Pango behind a measurement interface.

   Start with a main-thread text measurement service and an explicit
   cache key: text, font description, width, language, direction,
   spacing, white-space behavior, and relevant text-transform state.
   Batch worker requests so layout can ask for many measurements with
   one main-loop round trip. After that exists, run a small
   per-thread-Pango experiment separately.

5. Move inactive-tab layout behind snapshots.

   Once display lists and text measurement are explicit, try worker-side
   layout for inactive tabs first. Active-tab layout should move only
   after input latency, selection, scrolling, and incremental relayout
   still feel correct.

6. Define renderer IPC before adding renderer processes.

   Specify the message surface first: navigation commit data,
   subresource requests, response bodies, input, resize, timers, console
   messages, storage requests, display-list snapshots, and pixel buffers.
   Keep cookies, HSTS, HTTP cache, history, downloads, and persistent
   storage in the browser process.

7. Add process-per-site renderers after the IPC boundary is real.

   Use a forked renderer or zygote model that fits the current no-`execve`
   seccomp shape, then narrow Landlock/seccomp inside each renderer.
   Process-per-site is the right default compromise for memory and
   process count. Crashes should become tab/site failures, not whole
   browser failures.

## Recommended order

The practical sequence is:

1. texture fallback cleanup for rare decoder paths;
2. stylesheet import cleanup;
3. main-thread display-list generation and painting;
4. Pango measurement service;
5. inactive-tab worker layout;
6. renderer IPC protocol;
7. process-per-site sandboxed renderers.

This keeps each step small enough to verify locally while moving the
code toward the only true security boundary: a credential-less renderer
process with browser-owned networking, cookies, cache, and storage.
