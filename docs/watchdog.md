# Watchdog supervisor

Nordstjernen supervises itself. **The watchdog is on by default**: a
normal launch turns the process you start into a small supervisor that
spawns the real browser as a child, watches it, and restarts it
automatically if it crashes or hangs.

```sh
nordstjernen                      # supervised
nordstjernen https://example.com/ # supervised
```

All arguments are forwarded unchanged to the child, so the supervisor
is transparent.

## Disabling it

Three ways, highest priority first:

1. **Command line** (one launch): `--no-watchdog` runs the browser
   directly with no supervisor.

   ```sh
   nordstjernen --no-watchdog
   ```

2. **Environment** (one shell): `NS_NO_WATCHDOG=1` does the same.

3. **Config file** (persistent): set `watchdog_enabled = false` in
   `nordstjernen.conf`.

`--watchdog` forces supervision on even when the config file disables
it, which is handy for a one-off "wrap this run" without editing config.

One-shot and tooling modes are never supervised regardless of the
setting, because they are meant to run once and exit with a meaningful
status code: `--headless`, `--print-config`, `--dump=…`, `--eval=…`,
`--inspect=…`, and `--inspect-at=…`.

## How it works

The design mirrors how real browsers stay alive, split across the same
two roles they use:

1. **A parent supervisor restarts crashed children** — like Chrome's
   privileged browser process respawning a renderer, or Firefox
   respawning a content process. Our supervisor spawns the browser, waits
   on it with `g_child_watch`, and is notified the instant it exits. A
   clean exit (status 0 — you closed the last window) means "the user is
   done": the supervisor exits 0 and does *not* restart. Any abnormal
   exit — non-zero status, or a fatal signal such as `SIGSEGV` or
   `SIGABRT` — is a crash, and triggers a restart.

2. **An in-process thread catches hangs** — like Firefox's
   `BackgroundHangMonitor` and SpiderMonkey's watchdog thread, or
   Chrome's GPU watchdog. The browser bumps a counter from inside the GTK
   main loop every couple of seconds; a dedicated watchdog thread (which
   keeps running even when the main loop is wedged) watches that counter,
   and if it stops advancing for longer than the hang timeout it calls
   `_Exit(70)` (`NS_WATCHDOG_HANG_EXIT` in `src/watchdog.c`). The hang
   thus *becomes a non-zero exit*, which the supervisor in role 1 treats
   as a crash and restarts — so there is exactly one restart path, not
   two.

The hang timeout is **`js_eval_budget_ms` + 30 s** (90 s with the default
60 s JS budget). It is deliberately larger than the JS budget: a long
*synchronous* script blocks the GTK main loop — and therefore the
heartbeat — for as long as the budget allows, and that is normal, not a
hang. (Runaway scripts are interrupted separately, in-engine, at the JS
budget; see `src/js.c`.) The watchdog is the backstop for *native*
deadlocks — a wedged layout/paint loop or a stuck GTK call — which never
legitimately take that long.

There is no heartbeat file and no polling: the beat lives in process
memory, so nothing touches the filesystem on the hot path and there is no
world-writable temp file to harden.

## Crash-loop protection

A browser that crashes immediately on every launch would otherwise spin
forever. If the child crashes more than five times within 60 seconds the
supervisor gives up and exits non-zero instead of restarting again.
Successful restarts are spaced by a one-second backoff.

## Crash recovery

A silent relaunch to a blank page would throw away your work, so the
supervisor restores it — the way Chrome and Firefox reopen your tabs after
an "Aw, Snap!" / `about:tabcrashed`.

While the browser runs it records the URLs of its open `http(s)`/`file`
tabs to a small session file every few seconds. When a crash or hang
triggers a restart, the supervisor sets `NS_WATCHDOG_RECOVER=1` on the
respawned child; that child reopens the saved pages and shows a dialog
telling you it recovered after an unexpected exit. A clean shutdown
deletes the session file, so a normal launch never offers to recover.

The session file lives in the per-user runtime directory
(`$XDG_RUNTIME_DIR`, mode 0700 — e.g. `/run/user/<uid>/`), not in the
world-readable system temp directory.

## Shutting it down

On Unix, sending the supervisor `SIGINT` (Ctrl-C) or `SIGTERM` asks the
child to quit gracefully and then exits cleanly without restarting it.

## Notes

- The supervisor is a deliberately tiny process: it reads the config to
  learn whether it is wanted, then only spawns, watches, and restarts.
  It does not initialise the network stack, the sandbox, the seccomp
  filter, or any UI — those belong to the child.
- The child is launched with the internal `--watchdog-child` and
  `--watchdog-session=<path>` flags, and recovery is signalled with the
  `NS_WATCHDOG_RECOVER` environment variable; you never set these
  yourself.
