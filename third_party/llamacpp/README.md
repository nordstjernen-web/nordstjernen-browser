# Vendored llama.cpp (CPU-only)

Prebuilt static libraries and public headers from
[ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp), used by
`src/ai.c` to run a small language model on the CPU for the `about:start`
chat page. See `docs/ai.md`.

- Upstream commit: `49f354219059fc22316ae3efa54e54ba37f77860`
- Build: `cmake -DBUILD_SHARED_LIBS=OFF -DLLAMA_CURL=OFF -DGGML_NATIVE=OFF`
  (CPU backend only — no CUDA/Vulkan/Metal)
- License: MIT (see upstream `LICENSE`)

`lib/` holds `libllama.a` and the `libggml*.a` set; `include/` holds the
`llama.h` / `ggml*.h` public headers.
