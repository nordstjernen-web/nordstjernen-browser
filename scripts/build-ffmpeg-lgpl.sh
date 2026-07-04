#!/usr/bin/env bash
# Build a minimal, LGPL-licensed FFmpeg (libav*) carrying exactly the inline
# media Nordstjernen decodes — VP9/VP8 video and Opus/Vorbis audio in Matroska
# /WebM and Ogg containers — so it can be bundled into the redistributable
# macOS and Windows packages without the GPL obligations of a stock FFmpeg.
#
# `--disable-gpl --disable-nonfree --disable-version3` keeps the result LGPL
# v2.1+, and `--disable-autodetect` guarantees no external codec library
# (libx264, libx265, …) is linked — the enabled VP8/VP9/Opus/Vorbis decoders
# are FFmpeg's own native, LGPL implementations.
#
# Usage: scripts/build-ffmpeg-lgpl.sh <install-prefix>
# Prints the prefix on success; point pkg-config at "<prefix>/lib/pkgconfig".
#
# Env:
#   FFMPEG_VERSION   release tag without the leading n (default 8.1.1)
#   NS_FFMPEG_NOASM  set to 1 to configure --disable-x86asm (no nasm needed)
#   NS_FFMPEG_HOST   cross prefix passed as --cross-prefix (e.g. for mingw)
#   FFMPEG_EXTRA_CONFIGURE  extra configure flags (target-os/arch on cross)
set -euo pipefail

PREFIX=${1:?usage: build-ffmpeg-lgpl.sh <install-prefix>}
VER=${FFMPEG_VERSION:-8.1.1}
JOBS=${NS_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

log() { printf '[ffmpeg-lgpl] %s\n' "$*" >&2; }

TARBALL="$WORK/ffmpeg-${VER}.tar.gz"
fetched=0
for url in \
    "https://ffmpeg.org/releases/ffmpeg-${VER}.tar.gz" \
    "https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/n${VER}"; do
    log "fetching $url"
    if curl -fsSL --retry 3 --max-time 300 -o "$TARBALL" "$url"; then
        fetched=1; break
    fi
done
[ "$fetched" = 1 ] || { log "ERROR: could not download FFmpeg ${VER}"; exit 1; }

SRC="$WORK/ffmpeg-src"
mkdir -p "$SRC"
tar -xzf "$TARBALL" -C "$SRC" --strip-components=1

ASM_FLAG=--enable-x86asm
[ "${NS_FFMPEG_NOASM:-0}" = 1 ] && ASM_FLAG=--disable-x86asm

CROSS=()
[ -n "${NS_FFMPEG_HOST:-}" ] && CROSS+=(--cross-prefix="${NS_FFMPEG_HOST}-" --enable-cross-compile)

cd "$SRC"
# shellcheck disable=SC2086
./configure \
    --prefix="$PREFIX" \
    --disable-gpl --disable-nonfree --disable-version3 \
    --disable-autodetect \
    --disable-static --enable-shared \
    --disable-programs --disable-doc --disable-htmlpages --disable-manpages \
    --disable-debug --enable-pic \
    --disable-network --disable-avdevice --disable-avfilter \
    --disable-everything \
    --enable-avformat --enable-avcodec --enable-avutil \
    --enable-swscale --enable-swresample \
    --enable-demuxer=matroska,ogg \
    --enable-decoder=vp8,vp9,opus,vorbis \
    --enable-parser=vp8,vp9,opus,vorbis \
    --enable-protocol=file \
    $ASM_FLAG \
    "${CROSS[@]}" \
    ${FFMPEG_EXTRA_CONFIGURE:-}

make -j"$JOBS"
make install

# Reject any GPL/nonfree slip-up: config.h records the license decisions.
if grep -Eq '^#define CONFIG_(GPL|NONFREE) 1' config.h; then
    log "ERROR: built FFmpeg is not LGPL (GPL/nonfree enabled)"; exit 1
fi

log "installed LGPL FFmpeg ${VER} to $PREFIX"
printf '%s\n' "$PREFIX"
