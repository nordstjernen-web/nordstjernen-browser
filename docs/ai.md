# Local AI start page

`about:start` is a chat window backed by a small language model that runs
entirely on the CPU through an in-tree build of
[llama.cpp](https://github.com/ggml-org/llama.cpp). Nothing is sent over
the network: the prompt is tokenized, decoded, and detokenized in the same
process that fetches pages.

## How it works

- `src/ai.c` owns a single lazily-loaded `llama_model` + `llama_context`
  guarded by a mutex. `ns_ai_chat()` formats the message with the model's
  built-in chat template, runs a short sampled generation, and returns the
  reply text.
- `src/net.c` exposes it as an internal endpoint: a `fetch("about:ai?q=…")`
  from the start page is answered synchronously by `synthesize_about_response()`
  with `text/plain` generated tokens — no CORS, IPC, or sockets involved.
- The chat UI is the `about:start` HTML/JS template in `src/net.c`.

The static llama.cpp / ggml libraries and headers are vendored under
`third_party/llamacpp/` (CPU-only, no GPU backends).

## The model

The model file is **not** committed (it is hundreds of MB). The browser
looks for the first `*.gguf` it finds, in order:

1. `$NORDSTJERNEN_AI_MODEL` (an explicit file path)
2. `$XDG_DATA_HOME/nordstjernen/models/`
3. `./models/` (relative to the working directory)
4. `models/` next to the executable, and `../models/`

The reference model used during development is
**Qwen2.5-0.5B-Instruct** quantized to `Q4_K_M` (~470 MB), which answers a
short question in about a second on four CPU threads. Any GGUF chat model
with a built-in chat template works; drop it into one of the directories
above. If no model is found, the start page reports that the local model
is unavailable.
