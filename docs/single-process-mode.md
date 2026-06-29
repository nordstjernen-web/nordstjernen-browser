# Single-process mode

Nordstjernen's default architecture is **process-per-tab**: each tab's
engine runs in its own sandboxed `nordstjernen-renderer` process, and
the GTK shell is a thin client that blits a shared-memory
framebuffer and forwards input over an HTTP/JSON IPC control channel
(see [`tab-isolation.md`](tab-isolation.md)).

Passing **`--single-process`** to the desktop shell keeps everything
in one OS process instead:

```sh
./builddir/src/gtk/nordstjernen --single-process [URL]
```

Setting `NS_SINGLE_PROCESS=1` in the environment does the same (any
value other than `0` enables the mode). Multiprocess stays the
default; nothing changes unless the flag or the variable is given.

The task manager (Shift+Esc) labels tabs `in-process` in this mode and
reports the shell's own pid and memory, since there is no per-tab
renderer process to inspect.

## When to use it

- **Low-memory machines and containers** — one process means one copy
  of the engine's address space, caches, and font machinery instead of
  one per tab.
- **Environments where spawning helpers is awkward** — locked-down
  containers, missing `nordstjernen-renderer` binary, restrictive
  process limits.
- **Debugging** — every tab's engine lives in the shell process, so a
  single debugger session sees page loading, layout, JavaScript, and
  the UI at once, with no fork/exec to follow.

## How it works

Single-process mode reuses the renderer IPC protocol unchanged; only
the transport and process boundary differ.

- `src/rproc_http.c` — when the mode is enabled
  (`ns_rproc_http_set_inproc`), "spawning a renderer" creates a
  `socketpair` (two `_pipe`s on Windows) and a plain `malloc`'d
  framebuffer instead of forking `nordstjernen-renderer`. The shell's
  client code (`procview`/`procwindow`) is unchanged
  and does not know which mode it is running in.
- `src/rproc_inproc.c` — the in-process host. A small reader thread
  per tab parses requests off the control channel and forwards them to
  the **main-context thread** as idle sources on the default
  `GMainContext`, where they are handled strictly one at a time.
- `src/renderer_serve.c` — the request dispatcher
  (`/open`, `/render`, `/click`, …) shared verbatim between the
  out-of-process renderer executable and the in-process host, driving
  the same `libnordstjernen` engine API in both modes.

All engine work therefore runs on the thread that owns the default
GLib main context — the GTK main thread. This
matches the engine's threading model: its timers, async fetch
completions, and settle loops all live on the default main context.
The engine still uses its internal worker threads (tab workers, image
decode, networking, Dedicated Workers), so "single process" does not
mean single-threaded.

Framebuffers are not shared memory in this mode — client and renderer
are the same process, so the renderer paints straight into a buffer
the view blits from. No `memfd`/`shm` segments and no descriptor
passing are involved.

## Trade-offs

Single-process mode deliberately gives up the security and robustness
properties that process-per-tab exists for:

- **No renderer sandbox.** Untrusted page content is parsed, scripted,
  laid out, and painted inside the shell process. On Linux the shell
  keeps its Landlock filesystem confinement, but there is no per-tab
  seccomp boundary; a memory-safety bug in the engine is a bug in the
  whole browser. (Since no renderer is exec'd, the shell skips the
  exec-dir Landlock widening that proc mode needs.)
- **No crash containment.** An engine crash takes the whole browser
  down (the GTK watchdog still restarts it and recovers the session)
  instead of one tab showing a restart message.
- **Tabs share one engine lane.** Requests are serialized on the main
  context, so a slow page load can delay another tab's rendering; in
  multiprocess mode tabs make progress independently.

Use it where its footprint or debuggability is worth that trade; the
default remains process-per-tab.
