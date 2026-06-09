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
  - `about:ai-status` → JSON `{state, active, models:[…], …}` where `state` is
    `idle`, `downloading` (with the `downloading` id + `percent`/`received`/
    `total`), `ready`, `error`, or `disabled`, and `models` lists each tier
    with its size and an `installed` flag.
  - `about:ai-download?model=<id>` → selects a model and, if it isn't already
    on disk, starts the background download. Returns status.
  - `about:ai?q=…` → `text/plain` generated reply from the active model.
- The chat UI is the `about:start` HTML/JS template in `src/net.c`. On load it
  polls `about:ai-status`; if no model is installed it shows a picker of the
  available models, downloads the chosen one with a progress bar, then enables
  the chat. The active model is shown in the footer with a "change" control.

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

## Models

Model weights are **not** committed — they are downloaded on demand into the
user data directory (`$XDG_DATA_HOME/nordstjernen/models/` on Linux, the
platform-appropriate data dir elsewhere).

The browser offers a small catalog of CPU-friendly chat models (the `k_models[]`
table in `src/ai.c`), all Qwen2.5-Instruct `Q4_K_M` GGUFs from Hugging Face:

| Tier     | Model        | Size     |
|----------|--------------|----------|
| Fast     | Qwen2.5-0.5B | ~0.47 GB |
| Balanced | Qwen2.5-1.5B | ~1.0 GB  |
| Quality  | Qwen2.5-3B   | ~1.9 GB  |

Bigger models answer better but download more and run slower. The user picks a
tier on the start page; the loader switches between any installed models on
demand. Add or change tiers by editing `k_models[]`. Any GGUF chat model with a
built-in chat template works.

Two environment variables override the defaults:

- `NORDSTJERNEN_AI_MODEL` — an explicit path to an existing `.gguf` file;
  skips the catalog and download entirely.
- `NORDSTJERNEN_AI_MODEL_URL` — overrides the download source URL (the file is
  still saved under the selected tier's name); handy for mirrors or testing.
