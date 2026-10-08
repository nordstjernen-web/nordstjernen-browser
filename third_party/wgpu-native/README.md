# wgpu-native headers (vendored)

These are the C headers for [wgpu-native](https://github.com/gfx-rs/wgpu-native),
used by WebGPU (`src/webgpu.c`). Only the headers are vendored — the library
itself is located at build time via pkg-config (`wgpu_native`),
`-Dwgpu_native_root=/path/to/extracted/release`, or the pinned release fetched
by `subprojects/wgpu-native-<platform>.wrap` (glibc Linux, macOS, Windows).

- `include/webgpu/webgpu.h` — the multi-vendor standard C API
  (from webgpu-native/webgpu-headers; BSD-3-Clause).
- `include/webgpu/wgpu.h` — wgpu-native extensions (MIT OR Apache-2.0).

Pinned to wgpu-native release **v29.0.1.1**. To update: download the matching
release and replace these two headers (and bump the version in
`scripts/fetch-wgpu-native.sh` and the URLs/hashes in
`subprojects/wgpu-native-*.wrap`), then rebuild.

`scripts/fetch-wgpu-native.sh` downloads the matching release library for the
host platform into `dl/` (git-ignored) and prints its root, so the release
builds (`scripts/pack-linux.sh`, the nightly) can bundle `libwgpu_native.so`
beside the binaries. The library is never committed — only these headers are.
