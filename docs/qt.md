# Qt frontend (experimental)

Nordstjernen's reference UI is the GTK 4 frontend (`src/gtk/`). This document
covers the **experimental Qt 6 frontend** in `src/qt/` — a second, fully
independent browser shell over the same engine.

The Qt frontend is **off by default** (a meson feature option). Like the GTK
app, it is a **thin, process-per-tab shell**: `nordstjernen-qt` is a tabbed
browser in which **each tab drives its own sandboxed `nordstjernen-renderer`
process** over the shared-memory + control-channel IPC in `src/rproc_http.c`. The Qt
side blits the renderer's framebuffer and forwards scroll, resize, clicks,
hover, find, and navigation. Every tab shows the **full engine output** (the
same HTML/CSS/layout/paint/JS as the GTK frontend) and is isolated in its own
process. Passing `--single-process` instead serves every tab from an
in-process engine over the same protocol — see
[`single-process-mode.md`](single-process-mode.md); process-per-tab
remains the default.

## Architecture

The Qt shell links **no GTK**: it compiles `src/qt/*.cpp` plus the renderer
IPC client `src/rproc_http.c`, and in the default mode talks to the
`nordstjernen-renderer` process for everything page-related, keeping the
trusted UI process free of any untrusted-content parsing. The engine is
linked in statically only to back the opt-in `--single-process` mode
(`src/rproc_inproc.c` + `src/renderer_serve.c`); it stays idle unless that
flag is given.

| File | Role |
| --- | --- |
| `src/qt/main.cpp` | `QApplication` bootstrap; launches the tabbed `ProcWindow`. |
| `src/qt/procwindow.{h,cpp}` | Tabbed `QMainWindow`: a `QTabWidget` of `ProcView`s — one renderer process per tab — with a back/forward/reload toolbar, address bar, per-tab history, new-tab (Ctrl+T) / close-tab (Ctrl+W), Home, and a status bar. Middle-click or Ctrl+click on a link opens it in a new background tab (a fresh renderer process). |
| `src/qt/procview.{h,cpp}` | A `QAbstractScrollArea` that owns one `nordstjernen-renderer` process (via `src/rproc_http.c`), blits its shared framebuffer, and dispatches load/scroll/resize/hover/link/click/find IPC off the GUI thread. Per-tab back/forward history, mouse-wheel and keyboard scrolling (arrows, PageUp/Down, Space/Shift+Space, Home/End), per-tab zoom (Ctrl +/−/0, Ctrl+wheel) at the renderer's `scale`, `:hover` + pointer events, find-in-page (Ctrl+F), and a right-click context menu. |
| `src/rproc_http.{h,c}` | Parent/client side of the renderer IPC: shared-memory framebuffer + control channel (`socketpair`/`shm` on POSIX, pipes + file mapping on Windows). |

## Building

The frontend is gated behind a meson feature option, `qt`, whose default
is `disabled`. Enable it explicitly:

```sh
meson setup builddir -Dqt=enabled
meson compile -C builddir
./builddir/src/qt/nordstjernen-qt about:start
```

`-Dqt=auto` builds it only if Qt 6 is detected and silently skips it
otherwise; `-Dqt=disabled` (the default) never builds it. The
`nordstjernen-renderer` executable (always built) must be discoverable —
beside the `nordstjernen-qt` binary, or via the `NS_RENDERER` environment
variable.

### Dependencies

Qt 6 Core, Gui, Widgets, Concurrent, and Svg. The engine and its dependencies are
**not** linked into the Qt binary — they live in `nordstjernen-renderer`.

```sh
# Debian/Ubuntu
sudo apt install qt6-base-dev

# Fedora/RHEL
sudo dnf install qt6-qtbase-devel

# openSUSE
sudo zypper install qt6-base-devel

# macOS (Homebrew)
brew install qt

# MSYS2 / MINGW64
pacman -S mingw-w64-x86_64-qt6-base
```

## Status and limitations

Each tab shows full engine output, so HTML, CSS, layout, paint, and JavaScript
fidelity match the GTK frontend; the Qt client only blits and forwards input.
The renderer process sandboxes itself (Landlock + seccomp, see
`docs/tab-isolation.md`).

The Qt shell is at chrome parity with the GTK shell on browsing and dev
tooling: navigation/history, tabs (incl. Ctrl+PageUp/Down and Ctrl+Tab
switching), zoom, text selection, `:hover`, find-in-page (Ctrl+F), a
right-click context menu, the DevTools console (F12), save/export to PDF/PNG
(Ctrl+P/Ctrl+S), external media-player handoff (via `QDesktopServices`),
Ctrl+Q to quit, and an app menu with an About dialog — all over the shared
IPC messages (`FIND`, `CONSOLE`/`EVAL`, `EXPORT`, `MEDIA`). The address bar
sends a non-URL query to a search engine (the default DuckDuckGo Lite, since
the shell does not link the config store), and CJK/IME text entry works
through a `QInputMethodEvent` handler.

Still GTK-only: the **Settings** dialog (so the **configurable** search
engine) and **bookmarks**, because they edit the engine's `ns_config` /
`ns_bookmarks` stores, which the thin Qt shell deliberately does not link
(it links only Qt + `rproc_http.c`). Adding them would mean either pulling
those C modules (and glib) into the Qt binary or moving config/bookmarks
behind an IPC surface.
