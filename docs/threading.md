# Threading model

Nordstjernen is a **single-GUI-thread** application. GTK 4, the DOM,
CSS, layout, paint, and page JavaScript all live on the main thread.
Background threads exist only for work that is either inherently
blocking (network I/O) or CPU-heavy and GTK-free (HTML/CSS parsing,
image decoding). Every result computed off the main thread is handed
back to the main thread through a GLib main-context invocation before
it touches any shared browser state.

The guiding rule: **nothing outside the main thread ever touches a
`GtkWidget`, a `nd_node` DOM tree, a layout box, or a live
`JSContext` that belongs to the page.** Background threads operate on
self-contained inputs (response bytes, a URL, a script source) and
produce self-contained outputs (decoded pixels, a parsed stylesheet, a
detached DOM document) that are transferred by ownership.

## Threads in the process

| Thread | Created in | Lifetime | Touches GTK/DOM? |
|--------|-----------|----------|------------------|
| **Main / GUI** | `main()` | whole process | yes — owns everything |
| **Fetch pool** (≤6) | `net.c`, GLib `GTask` thread pool | per request | no |
| **Per-tab worker** (1 per tab) | `tab_worker.c` | tab lifetime | no |
| **Web Worker** (1 per `Worker`) | `js.c` | until terminated | no (own `JSContext`) |
| **WebSocket** (1 per socket) | `ws.c` | connection lifetime | no |
| **RNG warm-up** (transient) | `net.c` | one-shot at startup | no |
| **Watchdog hang monitor** | `watchdog.c` | whole process | no (reads one atomic) |
| **Watchdog supervisor** | separate process | whole process | n/a (own process) |

## How the main GUI stays responsive

Two mechanisms keep the UI from freezing:

1. **Blocking and heavy work is off-thread.** Network requests
   (`curl_easy_perform`), HTML parsing, CSS parsing, and image
   decoding never run on the GUI thread. They run on the fetch pool or
   the per-tab worker and post their results back.

2. **Long synchronous page JS cooperatively pumps the GUI.** Page
   JavaScript necessarily runs on the main thread (it manipulates the
   DOM). To stop a tight script from freezing the window,
   `nd_js_interrupt_cb` (`src/js.c`) is installed as the QuickJS
   interrupt handler. While *main-thread* JS runs, every ~100 ms it
   does up to 8 non-blocking iterations of the default `GMainContext`,
   so paints and input are serviced mid-script. This pump is gated to
   main-thread JS only (`!js->worker_host`): a Web Worker has its own
   thread and `GMainContext` and must never iterate the global default
   context from off the GUI thread.

If a script blows past the watchdog window despite all this, the
process is restarted (see below).

### Reentrancy contract of the GUI pump

Iterating the main context from inside a running script (mechanism 2)
re-enters the GLib/GTK event loop while a `JS_Eval`/`JS_Call` frame is
still on the C stack. Two layers keep that safe:

- **`js->in_pump`** is set for the duration of the pump. Every JS entry
  point that the loop could trigger — timers, `requestAnimationFrame`,
  event dispatch, image-load callbacks — early-returns when `in_pump`
  is set, so reentrant *JavaScript* never runs nested inside the pump.
- **`js->dispatch_depth`** is raised around event dispatch;
  `nd_js_free_or_defer_node` then parks freed DOM nodes in
  `orphan_nodes` and sweeps them after dispatch unwinds, so a handler
  can never free a node the outer frame still references.

The deliberately conservative consequence is that timers/rAF/events do
not fire *during* a long synchronous script — only paints do — until
the script yields.

**GTK-level teardown during the pump.** The two layers above guard
reentrant *JS*; they do not guard *GTK-level teardown*. If the pump
dispatches an input event whose handler frees `w->js` while a `JS_Eval`
frame is still on the stack, that is a use-after-free. The codebase
routes the well-understood cases around it: the explicit **Stop** path
calls `nd_js_halt` (set a flag) instead of freeing and the interrupt
handler unwinds on the next `halted` check; cross-origin navigation
frees the old context in the *load-completion* callback, not mid-eval;
and the post-navigation in-place pump (`nd_on_load_prepared`) re-checks
`nd_window_for_id` after iterating so it bails if the window vanished.

Every GTK path that can free or replace `w->js` is now deferred while
`nd_js_in_pump(w->js)` holds, so the running context cannot be pulled
out from under a live `JS_Eval` frame:

- **Close a tab** (`nd_browser_close_tab`, tab close button / Ctrl+W)
  and **zoom** (`nd_window_after_zoom`, which rebuilds the page) re-post
  themselves with `g_idle_add` keyed on the window id.
- **Window-manager close** (titlebar ✕, Alt+F4) is caught by a
  `close-request` handler (`on_toplevel_close_request`) that vetoes the
  close (returns `TRUE`) and re-posts `gtk_window_close` on idle while
  any tab in the window is mid-pump.
- **Navigation** (`nd_window_load_url` — omnibox, link, reload) defers
  via `nd_window_load_url_deferred` so a new load cannot complete and
  free the old context during the pump.

Each guard re-checks `in_pump` when its idle fires, so it simply
re-defers until the script yields (bounded by the eval budget / the
60 s monitor). On the normal path (`in_pump == FALSE`) every one of
these is byte-for-byte unchanged.

## Timeouts and budgets

Every blocking or unbounded operation has a bound:

| Operation | Bound | Where |
|-----------|-------|-------|
| HTTP connect | 15 s | `CURLOPT_CONNECTTIMEOUT`, `net.c` |
| HTTP transfer | 30 s default, 60 s max (`X-ND-Timeout-Seconds`) | `CURLOPT_TIMEOUT`, `net.c` |
| HTTP redirects | 10 | `CURLOPT_MAXREDIRS`, `net.c` |
| WebSocket connect | 15 s | `CURLOPT_CONNECTTIMEOUT`, `ws.c` |
| WebSocket transfer | none (long-lived); 10 ms poll cadence | `ws.c` |
| Page JS eval slice | `js_eval_budget_ms` (default 5 s, max 60 s) | `nd_js_budget_push`, `js.c` |
| Page JS hard monitor | 60 s of wall-clock per top-level entry | `ND_JS_MONITOR_LIMIT_US`, `js.c` |
| JS heap | `js_memory_cap_mb` (default 256, max 512) | `JS_SetMemoryLimit`, `js.c` |
| Per-origin in-flight requests | 6 | `ND_NET_MAX_PER_ORIGIN`, `net.c` |
| Total in-flight requests | 6 | `ND_MAX_CONCURRENT_FETCHES`, `net.c` |
| Hung GUI loop | `js_eval_budget_ms/1000 + 30` s | `watchdog.c` |

The origin-slot wait (`nd_net_acquire_origin_slot`) uses
`g_cond_wait_until` with a 250 ms re-check so a cancelled request never
blocks a pool thread indefinitely.

## Subsystem details

### Network (`src/net.c`)

`nd_net_fetch_async` / `nd_net_request_async` build a `GTask`,
duplicate every input into a heap `nd_fetch_ctx`, and enqueue it. A
mutex-guarded throttle (`g_fetch_throttle_mutex`, `g_fetch_queue`,
`g_fetch_active`) caps concurrency at `ND_MAX_CONCURRENT_FETCHES` and
dispatches via `g_task_run_in_thread`. `nd_fetch_thread` runs the
blocking `curl_easy_perform`, then `g_task_return_pointer` delivers the
`nd_response` to the `GTask` callback **on the main thread** (the task
was created there). On completion the active count is decremented under
the mutex and the queue is re-pumped.

Shared state and its protection:

- **Per-origin slots** (`g_origin_slots`, `g_origin_slots_lock`,
  per-slot `GCond`) — bounds concurrent connections per origin.
- **HSTS cache** (`g_hsts_cache`, `g_hsts_lock`) — reloaded from the
  curl HSTS file under lock, mtime-checked; read by every fetch.
- **curl share** (`g_share`, `g_share_locks[]`) — DNS/TLS-session/
  connection sharing across easy handles, with per-data-class locks
  installed via `CURLOPT_SHARE`.
- **Disk + memory response cache** (`cache.c`, `g_cache_mutex`;
  `bcache.c`, `g_lock`) — fully locked; safe to call from pool threads.
- **Config-derived globals** (`g_accept_encoding`, `g_ca_bundle`,
  `g_proxy_override`, `g_has_http3`, `g_allow_file_urls`) — written
  once at init / argument-parsing time, before any fetch is issued, and
  treated as read-only thereafter.

### Per-tab worker (`src/tab_worker.c`)

Each tab owns one serial worker thread (`GMutex` + `GCond` + `GQueue`).
It runs the GTK-free, CPU-heavy steps of a page load — `nd_html_decode_body`,
`nd_html_parse`, image decode, CSS parse/scope — off the GUI thread.
Jobs are submitted with an owned `nd_response` and a callback; the
thread produces an owned result (`nd_tab_load_result` etc.) and
delivers it with `g_main_context_invoke_full(NULL, …)` so the callback
runs on the main thread. Serial-per-tab means a tab's own work never
races itself; transferred ownership means it never shares a buffer with
the GUI thread. Shutdown drains the queue, signals, and joins.

### Web Workers (`src/js.c`, `nd_worker_*`)

A `Worker` spawns a dedicated thread with its **own `JSRuntime`,
`JSContext`, and `GMainContext`** (`nd_worker_host`). The worker thread
pushes its context as thread-default and runs its own `GMainLoop`;
timers and messages are sources on that context. The host is
reference-counted (`ref_count`) and uses atomics for cross-thread
flags (`closing`, `owner_alive`, `joined`); `host->lock` guards the
`worker_js`/`loop`/`thread` pointers.

Message passing is structured-clone over `JS_WriteObject` /
`JS_ReadObject` (no shared JS heap, transfer lists rejected):

- owner → worker: `g_main_context_invoke_full(host->context, …)` runs
  `nd_worker_deliver_worker` **on the worker thread**.
- worker → owner: `g_main_context_invoke_full(NULL, …)` runs
  `nd_worker_deliver_owner` **on the main thread**, guarded by
  `owner_alive`.

Teardown (`nd_js_free`) clears `owner_alive`, detaches owner pointers,
then `nd_worker_host_stop(host, TRUE)` sets `closing`, asks the loop to
quit, and joins. The interrupt handler observes `closing` and halts the
worker promptly.

### WebSockets (`src/ws.c`)

One thread per socket using curl `CONNECT_ONLY` plus `curl_ws_recv` /
`curl_ws_send`. The out-queue is a `GMutex`/`GCond` pair; `state`,
`exit_requested`, and `detached` are atomics. Inbound frames are
reassembled on the worker thread, then marshalled to the main thread
via `nd_ws_post` → `g_idle_add` → `nd_ws_dispatch_run`, which invokes
the JS callbacks. The `detached` flag (set when the owning JS object
goes away) makes any in-flight dispatch a no-op and is checked both at
post time and dispatch time; the dispatch holds a ref on `nd_ws` so the
struct outlives queued events.

`nd_ws_free` runs on the GUI thread and joins the worker, so the worker
must exit promptly. The recv/send poll loop checks `exit_requested`
every 10 ms, and the connecting handshake (`curl_easy_perform` with
`CONNECT_ONLY`) installs a transfer-info callback
(`nd_ws_handshake_progress`) that aborts the moment `exit_requested` is
set. Without it, closing a socket mid-handshake would block the GUI on
join for up to the 15 s connect timeout.

### Watchdog (`src/watchdog.c`)

Two parts. A **supervisor process** (`nd_watchdog_run_supervisor`)
`g_spawn`s the browser child, watches it with `g_child_watch_add`, and
restarts it on crash/hang with capped exponential burst control. Inside
the child, a **hang-monitor thread** (`nd_watchdog_hang_thread`) sleeps
1 s at a time and compares an atomic heartbeat (`g_beat`, bumped by a
2 s GUI-thread timeout) against a deadline; if the GUI loop stops
beating for longer than the budget it `_Exit`s with code 70 so the
supervisor restarts a fresh process. The monitor thread touches only
one atomic and never GTK.

### Debug log (`src/debuglog.c`)

The in-process event log is mutex-guarded (`g_dlog_mutex`) and is
emitted into from **any thread** — notably the fetch pool, which logs
every request from `nd_fetch_thread`. `nd_dlog_dispatch` snapshots the
subscriber list under the lock, then calls each listener **on the
emitting thread**. Listeners therefore must be thread-safe: they must
not touch GTK/DOM directly and must not dereference an object that
another thread can free. The console listener copies the entry and
hands it to the GUI thread via `g_idle_add`, keying on the window **id
by value** (not the `nd_window` pointer, which is freed when a tab
closes) so a late dispatch from a worker thread can never touch freed
memory; the idle callback resolves the id with `nd_window_for_id` and
no-ops if the window is gone.

### SQLite storage (`cache.c`, `history.c`, `idb.c`)

Three SQLite databases back the browser, each opened `WAL` +
`synchronous=NORMAL` with a 2.5 s `busy_timeout`:

- **HTTP cache** (`cache.c`) — read and written from the **fetch pool
  threads** (`nd_fetch_thread`); all access is serialised by
  `g_cache_mutex`.
- **History** (`history.c`) — a single persistent handle guarded by
  `g_history_mutex`, touched from the GUI thread.
- **IndexedDB** (`idb.c`) — **GUI thread only**. The backend primitives
  (`__nd_idb.*`) are called synchronously by the IndexedDB polyfill,
  which fakes async with `setTimeout`. Workers do not get IndexedDB, so
  no locking is needed.

IndexedDB previously **opened and closed the SQLite file on every
operation** (`open` + full `CREATE TABLE` schema + `close` per
`get`/`put`/…), which dominated the per-op cost and was the real
main-thread stall. Connections are now **cached** in a process-global
`key → handle` table (GUI-thread-only, so unlocked), opened and
schema-checked once; `deleteDatabase` evicts (closes) the cached handle
before unlinking the file and its `-wal`/`-shm`. The cache key is the
cheap raw `partition\x1fname` string, so a cache hit (the common case)
costs only a hash-table lookup — the expensive on-disk path (two SHA-256
hashes plus a `mkdir`/`chmod` of the partition directory) is computed
only on a miss, when the file is actually opened. Committed data persists
without an explicit close because WAL recovers it on the next open. The
cache is bounded to `ND_IDB_MAX_OPEN` (16) connections, LRU-evicted (each
handle carries a `last_used` stamp), and each connection caps its page
cache at 512 KiB (`PRAGMA cache_size=-512`), so a long session that
touches many origins cannot accumulate unbounded file descriptors or
SQLite page-cache memory; an evicted database simply reopens on next use. This
collapses a typical IndexedDB op from a file open/close round-trip to a
single indexed query — sub-millisecond on the GUI thread — so a
dedicated DB worker thread would only add dispatch overhead for the
common case; it is reserved for pathological bulk operations and is not
currently warranted.

All three databases process **attacker-influenced bytes** (response
bodies, visited URLs/titles, page-controlled IndexedDB values), so each
connection is hardened immediately after open: `DEFENSIVE` mode, load
extensions disabled, `TRUSTED_SCHEMA` off, double-quoted string literals
off (`DQS_DDL`/`DQS_DML`), and `SQLITE_OPEN_NOFOLLOW` where available.
IndexedDB additionally caps each database at `max_page_count` (256 MiB)
so a page cannot fill the disk.

## Invariants for contributors

- New blocking or >~10 ms CPU work belongs on the fetch pool or the
  per-tab worker, never inline on the GUI thread.
- A subscriber/callback that can be invoked from a background thread
  must capture identifiers by value (e.g. a window id), never a raw
  pointer to an object the GUI thread may free.
- A background thread returns results by **transferring ownership**
  through `g_main_context_invoke_full` / `g_idle_add`; it must not
  write GTK widgets, the DOM, layout, or the page `JSContext`.
- Every blocking call gets a timeout; every wait loop gets a bounded
  re-check or a cancellation check.
- Cross-thread flags are `g_atomic_*`; pointers shared across threads
  are guarded by the owning struct's mutex.
- Process-global state read by pool threads is either mutex-guarded
  (caches, HSTS, origin slots, curl share) or write-once-at-init.

## Synchronous sub-resource loads (pumped)

A handful of sub-resource loads are *synchronous from the script's point
of view* — the script that triggers them must see the result inline:
classic external `<script src>`, classic module imports (the QuickJS
module loader resolves synchronously), and `<iframe>` `src` plus its
inline scripts. (`XMLHttpRequest` is **not** here — it always uses the
async fetch pool, `nd_net_request_async`.)

Rather than block the GUI thread inside `curl_easy_perform`, these route
through `nd_js_fetch_resource`, which dispatches the request to the
async fetch pool and spins a **nested `GMainLoop`** on the GUI thread
until completion, with `js->in_pump` set for the duration. The window
keeps painting; reentrant JS (timers/rAF/events) is suppressed by the
existing `in_pump` guards, which preserves the synchronous-to-the-script
semantics; and every GTK teardown path defers (see the pump reentrancy
contract above), so the context cannot be freed mid-fetch. Completion is
a plain C callback, so it fires and quits the nested loop even while
`in_pump` defers everything else. The wait is bounded by the curl
transfer timeout, and concurrency still flows through the 6-slot
throttle. On a worker thread (`js->worker_host`) there is no GUI to keep
alive, so `nd_js_fetch_resource` falls back to a direct blocking fetch.

**Deferring without busy-spin.** Because the nested loop holds `in_pump`
for the whole sub-resource fetch (not just the ~8 iterations of the
interrupt-handler pump), any *other* JS-invoking completion that lands
during it must defer — and must do so without re-arming an
always-ready idle, which would spin the loop at 100 % CPU. The rule is
now uniform across **every** asynchronous source whose callback runs
page JS: each, when `in_pump` holds, re-arms with a 4 ms `g_timeout`
(matching the existing image-load path) so the loop can sleep in
`poll()` between checks. The covered set:

- `fetch()` completion (`nd_on_js_fetch_deliver_idle`),
- `XMLHttpRequest` completion (`nd_on_xhr_deliver_idle`) and the
  blocked/CORS-error path (`nd_xhr_emit_blocked_idle`),
- WebSocket open/message/close/error delivery, deferred in
  `nd_ws_dispatch_run` via an optional `busy` predicate on
  `nd_ws_callbacks` (so `ws.c` stays GTK-free and the queued payload is
  held, not copied, until the consumer is ready),
- `AbortSignal.timeout()` firing (`nd_abort_signal_timeout_fire`),
- `FileReader` completion (`nd_filereader_complete`),
- DOM timers (`nd_timer_fire`) and `requestAnimationFrame`/event entry
  points, which already early-out under `in_pump`.

This also closed a latent reentrancy gap: XHR, WebSocket, AbortSignal,
and FileReader callbacks previously ran immediately with no `in_pump`
check, so a completion arriving during *any* pump (including the brief
interrupt-handler pump) could run reentrant JS. With the set above,
no asynchronous source can run page JS while another JS frame is on the
stack.

