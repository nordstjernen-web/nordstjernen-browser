#!/usr/bin/env bash
# Build a macOS .app bundle + .dmg for Nordstjernen. Vendors the
# Homebrew GTK 4 dylibs with dylibbundler so the bundle is portable
# to Macs without Homebrew installed. Runs on macOS only.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(grep -E "^[[:space:]]*version" "$ROOT/meson.build" | head -1 \
          | sed -E "s/.*version: '([^']+)'.*/\1/")
ARCH=$(uname -m)
NAME=Nordstjernen
BUILDDIR=${BUILDDIR:-$ROOT/build-macos}
STAGE="$ROOT/dist/${NAME}.app"
DMG="$ROOT/dist/nordstjernen-${VERSION}-macos-${ARCH}.dmg"

mkdir -p "$ROOT/dist"

if [ "$(uname)" != "Darwin" ]; then
    echo "pack-macos.sh runs on macOS only." >&2
    exit 1
fi

if ! command -v dylibbundler >/dev/null 2>&1; then
    echo "dylibbundler not found. brew install dylibbundler" >&2
    exit 1
fi

run_dylibbundler() {
    local secs=$1; shift
    ( "$@" </dev/null ) &
    local pid=$! i=0
    while kill -0 "$pid" 2>/dev/null; do
        sleep 1
        i=$((i + 1))
        if [ "$i" -ge "$secs" ]; then
            echo "pack-macos.sh: dylibbundler exceeded ${secs}s, killing" >&2
            kill -9 "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            return 124
        fi
    done
    wait "$pid"
}

if [ ! -d "$BUILDDIR" ]; then
    meson setup "$BUILDDIR" \
        --prefix=/usr/local \
        --buildtype=release \
        -Db_lto=true \
        -Db_ndebug=true \
        --strip
fi
meson compile -C "$BUILDDIR"

rm -rf "$STAGE"
mkdir -p "$STAGE/Contents/MacOS"
mkdir -p "$STAGE/Contents/Resources/share/nordstjernen"
mkdir -p "$STAGE/Contents/Frameworks"

install -m755 "$BUILDDIR/src/gtk/nordstjernen" \
    "$STAGE/Contents/MacOS/nordstjernen-bin"
# The GUI spawns one sandboxed renderer process per tab; ship it alongside.
install -m755 "$BUILDDIR/src/nordstjernen-renderer" \
    "$STAGE/Contents/MacOS/nordstjernen-renderer"

cp "$ROOT/License.md" "$STAGE/Contents/Resources/share/nordstjernen/"
cp "$ROOT/THIRD-PARTY-LICENSES.md" "$STAGE/Contents/Resources/share/nordstjernen/"
cp "$ROOT/README.md" "$STAGE/Contents/Resources/share/nordstjernen/"

cat > "$STAGE/Contents/MacOS/Nordstjernen" <<'LAUNCHER_EOF'
#!/bin/bash
DIR=$(cd "$(dirname "$0")" && pwd)
BUNDLE=$(cd "$DIR/../.." && pwd)
export DYLD_LIBRARY_PATH="$BUNDLE/Contents/Frameworks:${DYLD_LIBRARY_PATH:-}"
exec "$DIR/nordstjernen-bin" "$@"
LAUNCHER_EOF
chmod +x "$STAGE/Contents/MacOS/Nordstjernen"

ICONSET=$(mktemp -d)
trap 'rm -rf "$ICONSET"' EXIT
ICON_GIF="$ROOT/data/icons/hicolor/scalable/apps/nordstjernen.gif"
if command -v sips >/dev/null 2>&1 && command -v iconutil >/dev/null 2>&1; then
    mkdir -p "$ICONSET/nordstjernen.iconset"
    for sz in 16 32 64 128 256 512; do
        sips -s format png -z "$sz" "$sz" "$ICON_GIF" \
            --out "$ICONSET/nordstjernen.iconset/icon_${sz}x${sz}.png" \
            >/dev/null 2>&1 || true
    done
    iconutil -c icns "$ICONSET/nordstjernen.iconset" \
        -o "$STAGE/Contents/Resources/nordstjernen.icns" >/dev/null 2>&1 || true
fi

cat > "$STAGE/Contents/Info.plist" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>Nordstjernen</string>
    <key>CFBundleDisplayName</key>
    <string>Nordstjernen</string>
    <key>CFBundleIdentifier</key>
    <string>org.nordstjernen.Nordstjernen</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleExecutable</key>
    <string>Nordstjernen</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleSignature</key>
    <string>NORD</string>
    <key>CFBundleIconFile</key>
    <string>nordstjernen.icns</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.utilities</string>
    <key>CFBundleURLTypes</key>
    <array>
        <dict>
            <key>CFBundleURLName</key>
            <string>HTTP URL</string>
            <key>CFBundleURLSchemes</key>
            <array>
                <string>http</string>
                <string>https</string>
            </array>
        </dict>
    </array>
</dict>
</plist>
PLIST_EOF

# Bundle both the launcher and the renderer: the renderer links the same
# image-codec dylibs (libavif, …) via the shared engine, so it needs its
# references rewritten to the bundled Frameworks too, or it fails to start
# with "Library not loaded: …/libavif.dylib".
if ! run_dylibbundler 300 dylibbundler -of -cd -b \
    -x "$STAGE/Contents/MacOS/nordstjernen-bin" \
    -x "$STAGE/Contents/MacOS/nordstjernen-renderer" \
    -d "$STAGE/Contents/Frameworks/" \
    -p "@executable_path/../Frameworks/"; then
    echo "pack-macos.sh: dylibbundler failed; listing binary dylibs and continuing" >&2
    otool -L "$STAGE/Contents/MacOS/nordstjernen-bin" || true
    otool -L "$STAGE/Contents/MacOS/nordstjernen-renderer" || true
    exit 1
fi

RES="$STAGE/Contents/Resources"
FW="$STAGE/Contents/Frameworks"

mkdir -p "$RES/share/icons"
cp -R "$ROOT/data/icons/hicolor" "$RES/share/icons/"
GTK_PREFIX=$(pkg-config --variable=prefix gtk4 2>/dev/null || true)
if [ -n "$GTK_PREFIX" ] && [ -f "$GTK_PREFIX/share/icons/hicolor/index.theme" ]; then
    cp "$GTK_PREFIX/share/icons/hicolor/index.theme" \
        "$RES/share/icons/hicolor/" 2>/dev/null || true
fi

# GTK 4 aborts at runtime if its org.gtk.gtk4.* schemas are missing, so
# collect schemas from every prefix that may carry them (glib's own
# schemasdir points into glib's Cellar and does NOT contain gtk4's
# schemas on Homebrew — the linked opt prefix does) and hard-fail if
# the compiled cache doesn't materialise.
SCHEMADIR=$(pkg-config --variable=schemasdir gio-2.0 2>/dev/null || true)
BREW_PREFIX=$(brew --prefix 2>/dev/null || true)
mkdir -p "$RES/share/glib-2.0/schemas"
for sd in "$SCHEMADIR" \
          "$GTK_PREFIX/share/glib-2.0/schemas" \
          "$BREW_PREFIX/share/glib-2.0/schemas"; do
    [ -n "$sd" ] && [ -d "$sd" ] || continue
    cp "$sd"/*.xml "$RES/share/glib-2.0/schemas/" 2>/dev/null || true
    cp "$sd"/gschema.dtd "$RES/share/glib-2.0/schemas/" 2>/dev/null || true
done
glib-compile-schemas "$RES/share/glib-2.0/schemas"
if [ ! -f "$RES/share/glib-2.0/schemas/gschemas.compiled" ]; then
    echo "pack-macos.sh: ERROR: gschemas.compiled was not produced" >&2
    exit 1
fi

PIXBUF_MODDIR=$(pkg-config --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0 2>/dev/null || true)
if [ -n "$PIXBUF_MODDIR" ] && [ -d "$PIXBUF_MODDIR" ]; then
    LOADERS="$RES/lib/gdk-pixbuf-2.0/2.10.0/loaders"
    mkdir -p "$LOADERS"
    cp "$PIXBUF_MODDIR"/*.so "$LOADERS/" 2>/dev/null || true
    for so in "$LOADERS"/*.so; do
        [ -e "$so" ] || continue
        run_dylibbundler 180 dylibbundler -of -cd -b -x "$so" -d "$FW/" \
            -p "@executable_path/../Frameworks/" >/dev/null 2>&1 || true
    done
    # Query against the ORIGINAL Homebrew loaders — the bundled copies
    # have been rewritten to @executable_path/../Frameworks refs that
    # only resolve inside the .app, so dlopen'ing them from here fails
    # and yields a header-only (useless) cache. Then strip the absolute
    # module dir prefix: the app sets GDK_PIXBUF_MODULEDIR at runtime
    # and relative entries are resolved against it.
    CACHE="$RES/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"
    GDK_PIXBUF_MODULEDIR="$PIXBUF_MODDIR" gdk-pixbuf-query-loaders \
        > "$CACHE"
    sed -i '' "s|\"${PIXBUF_MODDIR%/}/|\"|" "$CACHE"
    if ! grep -q '^"' "$CACHE"; then
        echo "pack-macos.sh: ERROR: loaders.cache has no loader entries" >&2
        exit 1
    fi
fi

rm -f "$DMG"
if ! hdiutil create -volname "Nordstjernen ${VERSION}" \
    -srcfolder "$STAGE" \
    -ov -format UDZO -imagekey zlib-level=1 \
    "$DMG"; then
    echo "pack-macos.sh: hdiutil create failed" >&2
    ls -la "$STAGE" || true
    exit 1
fi

echo
echo "Built: $DMG ($(du -h "$DMG" | cut -f1))"
echo "Bundle: $STAGE ($(du -sh "$STAGE" | cut -f1))"
echo
echo "Test:  open '$STAGE'"
echo "       '$STAGE/Contents/MacOS/Nordstjernen' --headless --dump=text about:start"
