# Nordstjernen for iOS

iOS support is at the **engine-portability stage**. The clean-room browser
engine — HTML parsing (lexbor), CSS cascade + layout, JavaScript (QuickJS),
image decoding (Wuffs) and cairo painting — is the same `libnordstjernen`
embedding library (`src/libnordstjernen.h`) the desktop and Android builds use.
iOS is architecturally an **Android-style port, not a macOS one**: the macOS
build ships the full GTK 4 desktop app, but GTK 4 does not run on iOS, so iOS
drives the GTK-free engine from a native (UIKit/Swift) shell the same way
Android drives it from a Kotlin/JNI shell.

## Architecture (planned, mirrors Android)

```
 UIKit/Swift UI  (URL bar + scrolling render surface)
        │  C embedding API (src/libnordstjernen.h)
 libnordstjernen  (engine: net, dom, css, layout, js, paint)
        │
        └─► glib · gobject · gio · cairo · pango · pangocairo ·
            harfbuzz · freetype · fontconfig · libcurl · sqlite3 ·
            uchardet · libpsl   (cross-compiled for iOS)
```

As on Android, iOS drops **GTK 4, librsvg and gdk-pixbuf**: `GdkTexture` is
replaced by the `ns_texture` abstraction (`src/texture.c`), the SVG / fallback
image decoders are gated out, and `ns_browser_render_argb32()` /
`ns_browser_render_rgba()` paint a viewport region straight into a bitmap. The
engine already builds GTK-free (the Android and renderer-only configurations
prove it), and its Darwin-specific paths (`getentropy`, `_NSGetExecutablePath`,
`sys/xattr.h`, `sandbox.h`) are the same on iOS as on macOS.

## What exists today

* **Engine iOS-portability CI** — `.github/workflows/ios.yml` runs on every
  push and PR. It builds the GTK-free desktop engine on a macOS runner (for
  `compile_commands.json` and the generated headers), then cross-checks **every
  engine translation unit against the real iOS SDK** — both device
  (`arm64-apple-ios`) and simulator (`arm64-apple-ios-simulator`) — via
  `ios/scripts/check-ios-sources.sh`. This is the iOS analogue of
  `android/scripts/check-android-sources.sh`: it catches iOS source regressions
  in the engine early, without needing a cross-compiled dependency sysroot.

## What a full .ipa still needs

The CI above verifies the engine's **own C** is iOS-clean. Producing an
installable app additionally requires, none of which exists yet:

1. **An iOS dependency sysroot** — glib/gobject/gio, cairo, pango/pangocairo,
   harfbuzz, freetype, fontconfig, libcurl, sqlite3, uchardet and libpsl
   cross-compiled for `arm64-apple-ios` (device) and the simulator. The Android
   equivalent is produced by a separate repository and published as a prebuilt
   sysroot; iOS wants the same.
2. **A meson iOS cross file** driving the engine build against that sysroot,
   plus a UIKit/Swift host app (URL bar + a `PageView`-style render surface
   over the engine's ARGB output), the way `android/` hosts the Kotlin shell.
3. **Signing / provisioning** for on-device installs and TestFlight.

Until (1) and (2) land there is no `.ipa`; the workflow here is the foundation
that keeps the engine iOS-ready while the rest of the port is built.

## Running the check locally

Requires macOS with the Xcode command-line tools (for the iOS SDK) and the
desktop build dependencies (see `docs/macOS.md`):

```sh
meson setup builddir -Dgtk=disabled -Dai=disabled
meson compile -C builddir
ios/scripts/check-ios-sources.sh device builddir      # arm64-apple-ios
ios/scripts/check-ios-sources.sh simulator builddir   # arm64 simulator
```
