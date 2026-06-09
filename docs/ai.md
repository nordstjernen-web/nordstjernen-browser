# Local AI start page

`about:start` is a chat window backed by a small language model that runs
entirely on the CPU through [llama.cpp](https://github.com/ggml-org/llama.cpp).
Inference is fully local: once the model is on disk, no network is touched —
the prompt is tokenized, decoded, and detokenized in the same process that
fetches pages.

## How it works

- `src/ai.c` (C) owns a single lazily-loaded `llama_model` + `llama_context`
  guarded by a mutex. `ns_ai_chat()` formats the message with the model's
  built-in chat template, runs a short sampled generation, and returns the
  reply text. It also manages a background model download and exposes a
  status state machine.
- `src/net.c` exposes three internal endpoints that the start page calls with
  `fetch()` — answered synchronously by `synthesize_about_response()`:
  - `about:ai-status` → JSON `{state, ...}` where `state` is `idle`,
    `downloading` (with `percent`/`received`/`total`), `ready`, `error`, or
    `disabled`.
  - `about:ai-download` → kicks off the background download and returns status.
  - `about:ai?q=…` → `text/plain` generated reply.
- The chat UI is the `about:start` HTML/JS template in `src/net.c`. On load it
  polls `about:ai-status`; if no model is present it offers a one-click
  download with a progress bar, then enables the chat once the model is ready.

## Building

llama.cpp is built **from source** as a Meson CMake subproject (no prebuilt
binaries are vendored), so it works on Linux, macOS, and Windows (MSYS2
MINGW64). The wrap is pinned in `subprojects/llama.cpp.wrap`; Meson fetches
and builds it (CPU-only: no CUDA/Vulkan/Metal, OpenMP off) at configure time.

The feature is controlled by the `ai` Meson option (enabled by default):

```sh
meson setup builddir -Dai=enabled   # default
meson setup builddir -Dai=disabled  # build without the local model
```

When disabled, the start page reports that the build has no AI support.

## The model

Model weights are **not** committed — they are downloaded on demand into the
user data directory (`$XDG_DATA_HOME/nordstjernen/models/` on Linux, the
platform-appropriate data dir elsewhere). The browser uses the first `*.gguf`
it finds there.

The default model is **Qwen2.5-0.5B-Instruct** quantized to `Q4_K_M` (~470 MB),
which answers a short question in about a second on four CPU threads. Any GGUF
chat model with a built-in chat template works.

Two environment variables override the defaults:

- `NORDSTJERNEN_AI_MODEL` — an explicit path to an existing `.gguf` file;
  skips the download entirely.
- `NORDSTJERNEN_AI_MODEL_URL` — the URL to download the model from (defaults
  to the official Hugging Face Qwen2.5-0.5B-Instruct-GGUF release).
