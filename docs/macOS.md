# Nordstjernen on macOS — build and run

This document records the working setup for building and running
Nordstjernen on modern macOS (Apple Silicon and Intel). The macOS CI
workflow (`.github/workflows/macos.yml`) is the authoritative spec;
what follows is the same recipe verified on a local MacBook.

The macOS build uses **Homebrew** for the toolchain and dependencies,
the same source the CI workflow installs. No Xcode project, no
CocoaPods. The meson build produces a single Mach-O executable
linking the same GTK 4 / libcurl / Cairo / Pango / lexbor
libraries shipped to Linux and Windows users;
`scripts/pack-macos.sh` wraps that binary into a `.app` bundle and
`.dmg` for distribution.

Tested against macOS 14 (Sonoma) and macOS 15 (Sequoia) on
`arm64` (M1/M2/M3/M4) and `x86_64` MacBooks.

## One-time setup

Install [Homebrew](https://brew.sh) if you do not already have it,
then:

```sh
brew install meson ninja pkg-config cmake gtk4 libepoxy curl \
    uchardet librsvg libpsl sqlite webp sdl2
brew install ccache    # optional, speeds up rebuilds
```

Homebrew installs to:

- `/opt/homebrew` on Apple Silicon (arm64).
- `/usr/local` on Intel (x86_64).

Both prefixes are wired up through `pkg-config` automatically; no
extra environment variables are needed for a normal user install.

## Build

```sh
git clone https://github.com/nordstjernen-web/nordstjernen
cd nordstjernen
meson setup builddir
meson compile -C builddir
```

QuickJS and lexbor are vendored in-tree at `src/quickjs/` and
`src/lexbor/`, so `meson setup` only auto-downloads the Wuffs
image-decoder subproject. No git submodules.

If you prefer the same flags CI uses:

```sh
export CC="ccache clang"
meson setup builddir --werror
meson compile -C builddir
```

## Run

```sh
./builddir/src/gtk/nordstjernen https://example.com
```

The first launch creates the per-user state directories under
`~/.config/nordstjernen/`, `~/.cache/nordstjernen/`, and
`~/.local/share/nordstjernen/`. GLib does **not** translate XDG base
directories to the macOS-native `~/Library/...` paths — every GTK app
on macOS follows the same Unix convention. Set `XDG_CONFIG_HOME`,
`XDG_CACHE_HOME`, or `XDG_DATA_HOME` to relocate.

Headless rendering works the same as on Linux:

```sh
./builddir/src/gtk/nordstjernen --headless --dump=text https://example.com
./builddir/src/gtk/nordstjernen --headless --dump=png:/tmp/page.png https://example.com
```

## Opening an unsigned build

The released `.dmg` is signed **ad-hoc**, not with an Apple Developer ID,
and is not notarised (the project has no paid Apple Developer account).
macOS stamps anything downloaded with a `com.apple.quarantine` flag, and
Gatekeeper refuses to open a quarantined app that isn't notarised — the
first launch fails with *"Nordstjernen is damaged and can't be opened"*
or *"cannot be opened because the developer cannot be verified."* This is
expected for an unsigned build and does not mean the download is corrupt.

Clear the quarantine flag once, after copying the app to `/Applications`:

```sh
xattr -dr com.apple.quarantine /Applications/Nordstjernen.app
```

The app then launches normally. (Right-click → **Open** also works on some
macOS versions, but the `xattr` command is the reliable path.) A
maintainer with a Developer ID can remove this step entirely by signing
and notarising the `.dmg` — see **Code signing** below.

## Platform notes

- **CA bundle.** Homebrew's `libcurl` links against OpenSSL, which
  has no built-in trust store and — unlike the deprecated SecureTransport
  backend — no bridge to the macOS Keychain. For a *developer* build
  Nordstjernen probes the standard Homebrew and system paths at startup
  (`/opt/homebrew/etc/ca-certificates/cert.pem`,
  `/usr/local/etc/ca-certificates/cert.pem`, `/etc/ssl/cert.pem`,
  and a handful of `openssl@3` variants) and points libcurl at
  whichever exists. A clean Mac has none of these (no Homebrew, and
  Apple ships no `/etc/ssl/cert.pem`), so `scripts/pack-macos.sh`
  **vendors a CA bundle** into the `.app` at
  `Contents/Resources/etc/ssl/certs/ca-bundle.crt` and
  `ns_macos_anchor_gtk_data()` exports `CURL_CA_BUNDLE` / `SSL_CERT_FILE`
  to it — the spawned renderer inherits those — so HTTPS verifies on a
  machine with no Homebrew. Override with `SSL_CERT_FILE` or
  `CURL_CA_BUNDLE` to ship your own bundle.
- **Self-path resolution.** The new-window action re-execs the
  current binary; on macOS the binary path is resolved with
  `_NSGetExecutablePath(3)` and canonicalised with `realpath(3)`.
- **Sandbox.** The Linux Landlock sandbox is Linux-only. On macOS,
  the only startup-side restriction is the same refuse-root check
  used on Linux — `nordstjernen` exits with status 77 if launched
  via `sudo` unless `NS_ALLOW_ROOT=1` is set.
- **Keyboard shortcuts.** Every accelerator uses GTK's `<Primary>`
  modifier, which maps to ⌘ on macOS. So `⌘L` focuses the URL bar,
  `⌘T` opens a new tab and `⌘N` a new window, `⌘R` reloads, `⌘W`
  closes the window, `⌘F` opens find-in-page, `⌘+` / `⌘-` / `⌘0` zoom, `⌘P`
  prints, `⌘⇧J` opens the JS console. Toolbar tooltips render the
  Mac glyphs (`⌘N`, `⌘⇧J`) instead of the Linux/Windows `Ctrl+...`
  strings.
- **Display server.** GTK 4 on macOS uses the native Quartz backend.
  There is no X11 or Wayland requirement, and no XQuartz dependency.
- **Packaging.** The meson output is a plain Mach-O binary in
  `builddir/src/gtk/nordstjernen` that launches from Terminal or Finder
  and shows up in the Dock while it runs. For distribution,
  `scripts/pack-macos.sh` stages a `.app` bundle (with a generated
  `Info.plist`) and produces a `.dmg`. The bundle executable is the real
  Mach-O (`Contents/MacOS/Nordstjernen`), not a wrapper script —
  `dylibbundler` rewrites every dependency to
  `@executable_path/../Frameworks`, so no `DYLD_LIBRARY_PATH` shim is
  needed and the whole `.app` can be code-signed. The renderer
  (`nordstjernen-renderer`) and the audio helper (`nordstjernen-audio`,
  present whenever SDL2 was found at build time) ship beside it and are
  run through `dylibbundler` too. Beyond the dylibs, the bundle carries
  the data GTK 4 reads at runtime so the `.dmg` works on a Mac with no
  Homebrew: the app's own SVG toolbar icons under
  `Contents/Resources/share/icons`, the GdkPixbuf loader modules plus a
  `loaders.cache` (the SVG loader is what renders those icons), the
  compiled GSettings schemas, and the vendored CA bundle. At startup
  `ns_macos_anchor_gtk_data()` (in `src/gtk/appmain.c`) points
  `GSETTINGS_SCHEMA_DIR`, `GDK_PIXBUF_MODULE_FILE`,
  `GDK_PIXBUF_MODULEDIR`, and `CURL_CA_BUNDLE` at those bundled copies
  when it detects it is running from inside an `.app`.
- **Architecture.** CI builds one `.dmg` per architecture — `…-arm64.dmg`
  on the `macos-15` (Apple Silicon) runner and `…-x86_64.dmg` on the
  `macos-13` (Intel) runner. An arm64 binary will not launch on an Intel
  Mac (Rosetta only translates x86_64 → arm64, never the reverse), so
  publish both; the x86_64 build also runs on Apple Silicon under Rosetta
  and is therefore the widest-compatibility single download.
- **Code signing.** `pack-macos.sh` signs the bundle inside-out (nested
  dylibs and helpers first, the `.app` last). With no Apple Developer ID
  it signs **ad-hoc** (`codesign --sign -`), which seals the bundle so it
  launches once the download quarantine is cleared (see below). Set
  `MACOS_SIGN_IDENTITY` to a `Developer ID Application: …` identity to
  produce a hardened-runtime, notarisation-ready bundle instead; the JIT
  entitlements in `packaging/macos/entitlements.plist` are applied on that
  path. Notarising still needs a maintainer's credentials:
  `xcrun notarytool submit "$DMG" --apple-id … --team-id … --password … --wait`
  then `xcrun stapler staple "$DMG"`.

## Definition of done on macOS

Same as everywhere else (`CLAUDE.md`):

1. `meson compile -C builddir` finishes without new warnings under
   the configured Clang flags.
2. The browser launches and the affected UI path works manually.
3. The change is committed and pushed to `origin/main`.

The macOS CI workflow runs on every push and pull request to `main`
plus manual `workflow_dispatch`, and exists to catch regressions that
the local Linux build misses (Apple Silicon ABI, BSD libc, GTK 4
Quartz backend).

## iPhone / iPad — not yet, and what would it take

There is no iOS build today. The blockers are real, not speed-of-light:

1. **No GTK 4 on iOS.** GTK 4 has no UIKit backend; the Quartz
   backend on macOS uses the same desktop AppKit APIs (`NSWindow`,
   `NSEvent`) that iOS does not expose. The window/toolbar/keyboard
   layer in `src/gtk/procwindow.c` / `src/gtk/procview.c` and the entry
   point in `src/gtk/appmain.c` would have to be replaced wholesale by a
   UIKit / SwiftUI shell.
2. **App Review until very recently required WebKit.** Apple's
   App Store Review Guidelines §2.5.6 historically forced every
   browser-like app to render web content through WebKit's
   `WKWebView`. The EU's Digital Markets Act has cracked this open
   for users in the EU as of 2024, but the global rule still
   stands and the entitlements / notarisation paperwork for an
   alt-engine browser is non-trivial.
3. **No `fork`/`exec` for per-tab renderers.** iOS apps run a single
   process. The per-tab renderer spawn (`ns_rproc_http_spawn`) that
   re-execs the binary for OS-level isolation simply cannot exist;
   isolation would need to be inside one process, or shelved.
4. **Cairo and Pango are not first-class on iOS.** They build,
   but no one ships them in App Store apps; the native path is
   CoreGraphics + CoreText, which means re-targeting the paint
   layer.

A realistic incremental path, if it ever gets prioritised:

- Keep the parser / CSS / layout / paint engine portable C
  (already true today). Compile it as a static library for the
  `arm64-apple-ios` triple.
- Wrap the rendering output in a thin UIKit (or SwiftUI) shell —
  `UIScrollView` containing a single `CALayer`-backed view that
  the engine paints into via Cairo's image surface, copied to a
  `CGImage`.
- Networking via `libcurl` continues to work on iOS as long as it
  is linked statically; `_NSGetExecutablePath` and CA bundle
  discovery already do the right thing.
- Skip Landlock, skip the refuse-root check (iOS apps are not
  root), skip the JS console UI for v1.

None of the above is on the roadmap; this section exists so the
next person who asks "could Nordstjernen run on iPhone?" has the
short answer in one place.
