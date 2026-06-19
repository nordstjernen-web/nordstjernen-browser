# wgpu-native headers (vendored)

These are the C headers for [wgpu-native](https://github.com/gfx-rs/wgpu-native),
used by the optional, off-by-default WebGPU feature (`-Dwebgpu=enabled`,
`src/webgpu.c`). Only the headers are vendored — the library itself is large
and is located at build time via pkg-config (`wgpu_native`) or
`-Dwgpu_native_root=/path/to/extracted/release`.

- `include/webgpu/webgpu.h` — the multi-vendor standard C API
  (from webgpu-native/webgpu-headers; BSD-3-Clause).
- `include/webgpu/wgpu.h` — wgpu-native extensions (MIT OR Apache-2.0).

Pinned to wgpu-native release **v29.0.0.0**. To update: download the matching
release, replace these two headers, and rebuild with `-Dwebgpu=enabled`.
