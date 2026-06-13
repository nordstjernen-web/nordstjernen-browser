# Experiment: HTTP/JSON vs binary+shm for the renderer IPC

This started as an experiment to answer a question raised in review — *could we
have used HTTP for the renderer IPC, and would it be easier to understand and
maintain?* — and it became the answer: **the renderer IPC is now HTTP/JSON for
control plus shared memory for frames**, wired into both the GTK and Qt shells,
replacing the previous binary-struct + double-buffered-slot protocol.

`src/rproc_http.{c,h}` (client) + `src/ipc_http.{c,h}` (framing) + the
`nordstjernen-renderer` built from `src/renderer_http.c` (server) are the
production renderer link. The headless `ipc-http-demo` (build with
`-Dipc_experiment=true`) exercises every operation. The sections below record the
performance comparison that justified the switch — the short version is that with
pixels in shared memory the wire format of the small control messages is a
rounding error, so the readable one wins.

## What was built

| Piece | Files | Role |
| --- | --- | --- |
| HTTP/JSON framing | `src/ipc_http.{h,c}` | buffered HTTP/1.1 read/write + a minimal JSON scanner/escaper |
| HTTP renderer | `src/renderer_http.c` | the engine behind a full HTTP server (see message catalog below) |
| HTTP client | `src/rproc_http.{h,c}` | mirrors the `ns_rproc_*` API over HTTP (open/render/link/click/key/hover/find/viewport/select/eval/console/media/close) |
| Demo | `src/ipc_http_demo.c` | drives a real page through every operation and prints the results |

### The model: HTTP for messages, shared memory for frames

Every control message is a plain HTTP `POST` with a JSON body; only the rendered
pixels travel through shared memory. The whole protocol is this catalog:

| Request | Body | Response |
| --- | --- | --- |
| `POST /open` | `{url,width,height,settle_ms}` | `{ok,page_width,page_height,title,url}`. `settle_ms` is a cap, not a fixed wait: the settle loop exits as soon as the page has been quiet (no pending fetches/XHRs/timers/animation-frame callbacks/image loads, empty main context) for a few consecutive ticks, so simple pages open in tens of milliseconds while busy pages get the full window |
| `POST /render` | `{width,height,scroll_x,scroll_y,scale}` | `X-W/X-H/X-Stride/X-Anim` headers; **pixels in shm**, empty body. When the engine reports nothing changed since the last frame and the viewport/scroll/scale are identical, the reply is `X-Unchanged: 1` with no repaint — the client keeps its cached frame. `X-Anim`/`X-Nav`/`X-WebGL` still ride on unchanged replies, so the frame loop and pending navigations are unaffected |
| `POST /link` `/click` `/select` | `{x,y[,mods\|kind]}` | `{href}` (`/link` also returns the computed `cursor` keyword) |
| `POST /key` | `{kind,key,code,keycode,mods}` | `{href}` |
| `POST /hover` | `{x,y}` | `{changed,href,cursor}` — one pointer-motion round trip carries the `:hover` restyle result, the link under the pointer, and the computed CSS cursor keyword, so the shells send a single message per mouse move |
| `POST /release` | — | `{changed}` |
| `POST /find` | `{query,case_sensitive,direction,from_y}` | `{total,current,scroll_y}` |
| `POST /viewport` | `{width,height}` | `{ok,page_width,page_height}` |
| `POST /eval` | `{src}` | `{text}` |
| `POST /console` | — | `{text}` |
| `POST /media` | `{x,y}` | `{url,is_video,stream}` |
| `POST /quit` | — | — |

The shared framebuffer is set up exactly once: the client creates an anonymous
`memfd`, passes the fd to the renderer over the control socket with `SCM_RIGHTS`,
and both `mmap` it. A `RENDER` reply just names the geometry; the pixels are
already in both address spaces. `src/ipc_http_demo.c` exercises the whole thing
headlessly — open a page, render a frame, run `eval`/`console`/`find`, hit-test a
link — so the design is visible end to end:

```
2. POST /open    -> ok=1  page=1280x140  title=""
3. POST /render  -> frame 1280x800 stride=5120 animating=0  (pixels in shm)
4. POST /eval    -> 1 + 2 = 3 ; window.answer = 42
5. POST /console -> hello from the page
6. POST /link    -> link at (4,84) -> https://example.com/page
7. POST /find    -> matches=1 current=1 scroll_y=89
```

The HTTP variant is the *natural* HTTP design: control messages are JSON request
bodies, and a rendered frame is returned as the raw pixel bytes in the response
body (`Content-Type: application/octet-stream`, geometry in `X-W`/`X-H`/`X-Stride`
headers). The production protocol keeps pixels in a shared-memory framebuffer and
sends only a fixed-size control struct.

## Method

Same engine, same pages, same viewports. For each size: 8 warm-up frames, then 800
timed `render` round-trips of the already-open page (no refetch). `RUSAGE_CHILDREN`
isolates renderer CPU; `RUSAGE_SELF` isolates client CPU. Run headless (software
Cairo), so it is fully reproducible in CI-like environments. Numbers below are
representative of two stable runs on this machine.

## Results

Three transports, same engine and workload:

- **binary+shm** — production: fixed structs, pixels in shared memory.
- **http+body** — pixels returned in the HTTP response body (no shm).
- **http+shm** — HTTP/JSON control, but pixels in a shared `memfd` (empty body);
  the realistic "fast HTTP" hybrid.

Per-frame render latency / client CPU over 800 frames, vs binary+shm:

| Viewport | binary+shm | http+body | http+shm |
| --- | --- | --- | --- |
| 800×600 (1.9 MB) | 0.51 ms / 26 ms | 0.94 ms (**1.8×**) / 339 ms (**13×**) | 0.53 ms (**1.04×**) / 38 ms (**1.5×**) |
| 1280×800 (4.1 MB) | 0.71 ms / 26 ms | 1.41 ms (**2.0×**) / 576 ms (**23×**) | 0.69 ms (**0.97×**) / 32 ms (**1.2×**) |
| 1920×1080 (8.3 MB) | 1.05 ms / 32 ms | 3.07 ms (**2.9×**) / 1592 ms (**50×**) | 1.09 ms (**1.04×**) / 37 ms (**1.2×**) |

Bytes over the socket per frame: binary+shm **60 B**, http+shm **240 B**,
http+body **the whole framebuffer** (1.9–8.3 MB, i.e. 32 000–138 000× more).

Two things stand out:

1. **The penalty scales with frame size** for http+body (1.8× → 2.9×, client CPU
   13× → 50×) because the entire framebuffer is copied through the socket every
   frame. The binary path's cost is flat (~30 ms client CPU, 60 B/frame at any
   resolution): pixels never cross the socket — the client reads a 28-byte header
   and the pixels are already mapped.
2. **http+shm matches binary** — 1.04× latency, ~1.2× CPU. So the entire 2–3×
   penalty was the pixel copy, **not** the HTTP/JSON parsing. With pixels in shm,
   HTTP's per-message control overhead is in the noise (microseconds of JSON
   scanning).

## Can it be made faster?

Yes, two levers, very different ceilings:

- **Keep pixels in the body, enlarge the socket buffers.** A 8 MB body does not fit
  the default ~200 KB socket buffer, so the kernel ping-pongs the two processes
  through dozens of write/read iterations. Forcing the socketpair's send/receive
  buffers to a whole frame (`SO_SNDBUFFORCE`/`SO_RCVBUFFORCE`) cut the 1080p latency
  gap from ~3.7× to ~2.5× — but **client CPU barely moved** (57× → 50×), because that
  cost is the *copy*, not the syscalls. This is the ceiling for pixels-in-body: you
  can shave the context-switch overhead, but never the two framebuffer copies.
- **Move pixels out of the body (http+shm).** This removes both copies and lands on
  binary's performance (above). But it reintroduces the entire shared-memory /
  `memfd` / `SCM_RIGHTS` machinery — so HTTP is now only the *control* protocol, and
  its "one simple mechanism" selling point is gone.

The real lesson: the performance question was never "binary vs HTTP." It was
"**pixels in shared memory vs pixels over the socket.**" The wire format of the
small control messages is irrelevant to throughput; the data plane is everything.

`open` latency (≈30 ms binary vs ≈46 ms HTTP) is dominated by real page
parse/layout in the shared engine; the ~15 ms delta is mostly measurement
placement, not protocol overhead, and isn't the interesting signal — the
data-plane render cost is.

## Understanding

HTTP genuinely wins here:

- **Readable on the wire.** `strace`/`tcpdump` shows `POST /render … {"width":1280,…}`.
  The binary protocol is opaque structs.
- **Self-describing and pokeable.** A malformed message is visible; you could drive
  the renderer with `curl`-like tooling.
- **Naive callers are safe.** The HTTP client copies pixels into its own buffer; the
  consumer has no lifetime contract to honor. The binary protocol's zero-copy slot
  model has a manual contract — *the caller must release each framebuffer slot* —
  and it is easy to forget. Concretely: the first version of this very benchmark
  forgot to release the slot and the binary path failed after two frames. That is a
  real maintainability hazard the HTTP design does not have.

## Maintaining

More mixed than "understanding" suggests:

- **HTTP needs parsers.** `ipc_http.c` is ~340 lines of hand-rolled HTTP + JSON
  before a single message is defined. The binary protocol needs none — a message is
  a `struct` and a bounded `read`. For the **security boundary** (the trusted parent
  parsing output from the untrusted, sandboxed renderer), a textual parser is more
  attack surface to harden than fixed-size struct reads with explicit length caps.
- **Per-op cost favors binary.** Adding an operation to the binary protocol is a
  struct plus ~15 lines. Adding one to HTTP is JSON build + JSON parse + dispatch,
  ~30–40 lines, on top of the parser. The raw line counts (HTTP stack 774 lines for
  3 ops, Linux-only; binary stack 2052 lines for 19 ops across two platforms with
  fd-passing and refcounted slots) understate this because the binary stack does far
  more.
- **The "just use HTTP" simplicity is partly illusory.** To regain the binary path's
  performance you would run *HTTP control + shm frames* — which reintroduces every
  bit of the shared-memory / `memfd` / `SCM_RIGHTS` / Windows-handle machinery **and**
  keeps the HTTP/JSON parser. That hybrid is more total complexity than either pure
  design, for the readability of the small control messages only.

## Verdict

For Nordstjernen's renderer link — two co-shipped processes on the same host, a
synchronous request/reply at up to 60 fps, a multi-megabyte pixel payload as the
dominant traffic, and a security boundary that runs from the sandboxed renderer to
the trusted shell — **binary structs + shared memory is the right tool.** Pure HTTP
(pixels in the body) is 2–3× slower per frame and 13–50× more client-CPU-hungry,
scaling worse exactly where it hurts. HTTP control + shm frames *does* match binary's
performance (measured: 1.04× latency), which proves the cost was always the data
plane — but it keeps the HTTP/JSON parser **and** all the shared-memory machinery, so
it is strictly more complexity than the binary protocol for the readability of the
small control messages only. The thing HTTP genuinely buys (wire-readability, no
slot-lifetime footgun) is real but does not justify either the pure-HTTP performance
loss or the hybrid's extra parser on a hardened parent.

HTTP (or WebSocket/gRPC) would become the better choice if the calculus changed:

- the renderer ran **remotely** (HTTP's transport, caching, and intermediaries
  finally pay off), or
- the frontends were **polyglot** (a language-agnostic protocol is worth real cost),
  or
- the payload were **not** a giant zero-copyable pixel buffer.

The cheap way to capture HTTP's *readability* win without its cost is the existing
roadmap item **P5**: a small self-describing `{magic, seq, type, length}` envelope on
the binary messages — framing you can resync and reason about, at 8 bytes/message and
no parser.
