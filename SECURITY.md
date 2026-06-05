# Security policy

Nordstjernen is a small clean-room web browser. Security fixes ship from
`main`; only the latest tagged release is supported.

## Reporting

File a regular GitHub issue:
<https://github.com/nordstjernen-web/nordstjernen/issues>

Please include the version (shown in the About Nordstjernen dialog),
your OS, a minimal reproducer (URL or self-contained HTML), and your
assessment of the impact.

## Threat model

Nordstjernen treats the Internet with the outmost suspicion. The attacker controls fetched
HTML, CSS, JavaScript, images, fonts, media, and PDFs. The user, the
kernel, and the local filesystem outside the sandbox allow-list are
trusted.

**In scope**

- Memory-safety bugs in our C code: out-of-bounds reads/writes,
  use-after-free, integer overflow, format-string.
- Linux sandbox escapes — both layers: the Landlock filesystem
  allow-list and the seccomp-bpf syscall allow-list.
- Windows process-mitigation bypass — the policies set via
  `SetProcessMitigationPolicy` at startup (ASLR, strict handle
  checks, extension-point disable, image-load restrictions,
  dynamic-code prohibition, child-process block).
- Same-origin, cookie, and HTTP-cache partitioning bypass.
- HSTS, mixed-content, and CSP enforcement bypass.
- URL-bar spoofing (IDN homograph, scheme confusion, etc.).

**Out of scope**

- Bugs in third-party libraries (libcurl, GTK 4, GLib, lexbor, QuickJS,
  Wuffs, librsvg, …). Report upstream; we update when fixes ship.
- Features we deliberately don't implement: WebGL, WebGPU, WebRTC,
  MSE/EME/DRM, service workers, browser extensions, JIT, "AI" web APIs.
- CPU-level side channels (Spectre-class).
- Attacks that already require local code execution as the same user.

## Defenses

The browser runs as a single process; there is no multi-process site
isolation. Defenses are layered so that a memory-safety bug in any one
component does not immediately yield arbitrary code execution outside
the user's data directory.

### Compile-time hardening (`meson.build`)

PIE, full RELRO, non-executable stack, separate-code segments,
`-fstack-protector-strong`, `-fstack-clash-protection`,
`-fcf-protection=full` (Intel CET / AMD IBT), `_FORTIFY_SOURCE=3`
(`=2` fallback), `-Wformat=2 -Wformat-security`. No JIT is used or
linked — W^X holds for the whole process, so any RCE primitive has to
work without writable executable pages.

### Privilege drop (`src/security.c`)

- Refuses to start as root (Linux/macOS) or Administrator (Windows).
  Override with `ND_ALLOW_ROOT=1` if you really mean it.
- Sets `PR_SET_NO_NEW_PRIVS` before installing the seccomp filter, so
  setuid binaries cannot be used to gain privileges after a compromise.

### Linux sandbox

Two independent layers, both default-deny, both installed at startup
before any HTML is parsed.

- **Landlock (filesystem).** Read-only access to system libraries
  (`/usr`, `/lib`, `/lib64`), `/etc`, the CA bundle, font caches,
  `/dev/urandom`, and the X11 / Wayland sockets. Read+write access to
  the per-user XDG config, data, and cache directories under
  `~/.config/nordstjernen`, `~/.local/share/nordstjernen`,
  `~/.cache/nordstjernen`. The rest of `$HOME` — `~/.ssh`, `~/.aws`,
  `~/.netrc`, other browsers' state, shell history — is **not**
  reachable. No directory the renderer can write to is also
  executable.
- **seccomp-bpf (syscalls).** Default-deny filter that returns `EPERM`
  for ~210 unrelated syscalls. `execve` / `execveat` are blocked, so a
  compromised renderer cannot pivot to another interpreter or binary
  even if Landlock would have allowed reading it. `ptrace`, `bpf`,
  `keyctl`, `mount`, `unshare`, `userfaultfd`, the `io_uring_*` family,
  `perf_event_open`, `kexec_load`, and the module syscalls are not on
  the list. TSYNC propagates the filter to every thread.
- **Media launcher broker.** Nordstjernen ships no audio/video codecs;
  playback is handed off to an external player. Because the browser
  process itself has `execve` blocked, a tiny launcher broker is forked
  at startup **before** either layer is installed and connected over a
  `socketpair`. The broker stays outside the sandbox; the browser stays
  inside it. When the user clicks an `<audio>`/`<video>` element the
  browser sends the broker **only** the resolved media URL — never a
  command line. The broker validates the scheme
  (`http`/`https`/`ftp`/`rtsp`/`rtmp`, or `file://` / an absolute path),
  rejecting anything with a leading `-` or control characters, then
  picks a player from a fixed allow-list (`mpv`, `vlc`, `celluloid`,
  `totem`, `mplayer`, `ffplay`) **itself** and `execve`s it with the URL
  as the single argument. A compromised renderer can therefore at most
  ask a known media player to open a scheme-checked URL — it cannot
  choose the binary, inject extra arguments, or pivot to an arbitrary
  interpreter. The broker exits as soon as the browser does (socket EOF).

Both layers can be disabled for debugging with `ND_NO_SANDBOX=1` /
`ND_NO_SECCOMP=1`. Don't use those in normal operation.

### Windows process mitigations

Windows has no direct Landlock or seccomp-bpf equivalent that a
user-space GTK process can apply to itself. Instead the browser
hardens itself at startup via `SetProcessMitigationPolicy`, called
from `nd_security_win32_mitigations_init` in `src/security.c`
**before** any DLL we don't statically link is touched. Six
policies, all best-effort (an unsupported policy on an older
Windows just returns `FALSE` and is skipped):

- **ASLR** (`ProcessASLRPolicy`, flags `0x0F`) — force relocate
  images, force bottom-up randomization, high-entropy 64-bit
  layout, disallow stripped images. Belt-and-braces on top of the
  PE header's `IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE`.
- **StrictHandleCheck** (`ProcessStrictHandleCheckPolicy`, flags
  `0x03`) — raise an exception on invalid handle use and lock the
  setting permanently. Catches double-close, UAF-of-handle bugs.
- **DisableDynamicCode** (`ProcessDynamicCodePolicy`, flags
  `0x01`) — refuse `VirtualAlloc`/`VirtualProtect` with
  `PAGE_EXECUTE_*`. Pairs with the no-JIT QuickJS guarantee: the
  process has no legitimate need for writable-executable memory,
  so any RCE that depends on allocating one is denied at the
  kernel boundary.
- **DisableExtensionPoints**
  (`ProcessExtensionPointDisablePolicy`, flags `0x01`) — block
  `AppInit_DLLs`, WinSock Layered Service Providers, Image File
  Execution Options debuggers, and a few other legacy injection
  vectors that load DLLs into every process on the box.
- **ImageLoad restrictions** (`ProcessImageLoadPolicy`, flags
  `0x07`) — `NoRemoteImages` (no DLL loads from UNC/mapped
  network drives), `NoLowMandatoryLabelImages` (no DLL loads from
  Low-IL filesystem locations), `PreferSystem32Images` (resolve
  ambiguous DLL names against system32 first). Mitigates DLL
  planting and search-order hijacks.
- **ChildProcess block** (`ProcessChildProcessPolicy`, flags
  `0x01`) — no `CreateProcess` from this process, so a compromised
  renderer cannot directly pivot to `cmd.exe` / `powershell` / another
  binary. Audio/video handoff goes through `ShellExecuteW`, which the
  shell performs out-of-process (not as our child) and only after the
  URL passes the same remote-scheme check used elsewhere
  (`http`/`https`/`ftp`/`rtsp`/`rtmp`; `file://` and local paths are
  refused on Windows so a media link can never launch a local
  executable).

There is no per-path filesystem sandbox; Windows AppContainer
would provide one but requires a manifest and code-signing
integration we don't have yet. The closest equivalents — Low
Integrity Level drop and AppContainer — are tracked as future
work.

Plus the same `nd_security_refuse_root` as Linux: refuses to
launch as Administrator (`CheckTokenMembership` against the
Built-in Administrators SID), with a `MessageBox` and a `stderr`
diagnostic.

The whole mitigation suite can be disabled for debugging with
`ND_NO_WIN32_MITIGATIONS=1`. Don't use that in normal operation.

### Network

libcurl drives every fetch with TLS verification enabled
(`CURLOPT_SSL_VERIFYPEER=1`, `CURLOPT_SSL_VERIFYHOST=2`), `http,https`
as the only allowed protocols, redirects clamped to HTTPS once the
initial scheme is HTTPS, max ten redirects, an explicit response-size
cap, and `CURLOPT_NOSIGNAL`. HSTS state is loaded and persisted via
`CURLOPT_HSTS`; Alt-Svc is honoured. Mixed-content sub-resources
(http inside an https document) are blocked.

### Origin isolation

- Cookies and the HTTP cache are partitioned per top-level **site**
  (scheme + registrable domain + port), where the registrable domain
  comes from the Public Suffix List via libpsl. All subdomains within
  the same registrable domain share one cookie jar and one cache
  partition; everything else is isolated. Third-party cookies are
  blocked by default.
- CSP (`default-src`, `script-src`, `style-src`, `img-src`,
  `media-src`, `connect-src`, `font-src`, `frame-src`,
  `frame-ancestors`) is parsed and enforced for both inline and
  external resources, including nonce and hash matches. Host source
  expressions match scheme, host (with `*.` wildcard), port (defaulting
  to the scheme's default), and path (left-anchored if the source ends
  in `/`, exact otherwise). `*` follows CSP3 semantics — it matches
  network schemes only, never `data:`, `blob:`, `filesystem:`, or
  `javascript:`.
- Subresource Integrity (`integrity="sha256-…"` / `sha384-` / `sha512-`)
  is verified against the response body before scripts or stylesheets
  are applied.
- IDN labels are accepted for display only under a Unicode TR-39
  "Highly Restricted"–style profile: each label must be either pure
  ASCII, a single non-Latin script, or one of the three standard CJK
  combinations (Japanese / Traditional Chinese / Korean). Anything
  else is shown as punycode in the URL bar, defeating most
  Latin-look-alike homograph attacks.

### On-disk state

Config, cookies, cache, HSTS, Alt-Svc, and bookmarks live under the
XDG dirs above with owner-only permissions (`0700` directories, `0600`
files on Unix; ACL-tightened on Windows). The HTTP cache is keyed on
`SHA-256(URL || partition)`, so cache filenames never embed
attacker-controlled bytes and no path-traversal is possible.

### Parsers

- HTML is parsed exclusively by [lexbor](https://github.com/lexbor/lexbor);
  there is no hand-rolled HTML tokenizer.
- URL parsing routes through lexbor's WHATWG URL module.
- PNG, GIF, BMP, and JPEG bytes are decoded by
  [Wuffs](https://github.com/google/wuffs) (memory-safe,
  transpiled-to-C). GdkPixbuf and librsvg handle the remaining formats
  inside the same sandbox.
- Charset sniffing is delegated to uchardet, not hand-rolled.

### JavaScript

JavaScript runs in [QuickJS](https://github.com/quickjs-ng/quickjs), an
interpreter — no JIT, no machine-code generation. The DOM/JS bridge
invalidates opaque pointers on node free and re-validates on every
call, so DOM mutation cannot dangle a JS-held handle.

Each tab has its own QuickJS runtime and context; tabs do not share JS
state. Within a single tab, navigating across origins (e.g. from
`news.example.com` to `evil.com`) tears down the runtime and starts a
fresh one, so attacker-controlled globals (`window.foo = secret;`),
prototype pollution, leftover module state, and any other in-memory
JS residue from the previous origin cannot reach the new origin's
scripts. Same-origin navigation reuses the existing runtime so
sessionStorage and history work as expected.

Iframes are rendered. An `<iframe src>` is fetched through the same
hardened network pipeline as any other resource (TLS verification,
`http`/`https` only, mixed-content blocking, redirect clamp, response-size
cap, CSP `frame-src`); `srcdoc` is parsed inline. The content document is
parsed by lexbor and laid out in place.

A loaded frame gets a JavaScript realm, but **within the parent tab's
single QuickJS runtime** — there is no separate runtime, context, or OS
process per frame. The realm is a synthetic scope: the frame sees its own
`window`, `document`, `location`, and `history`, and `top`/`parent`/`self`/
`frames` are redirected to the frame itself rather than exposing the parent's
real global. This is a best-effort JavaScript-level boundary for ordinary
content, **not** a hard security boundary the way per-tab runtimes or the
cross-origin top-level navigation teardown (above) are: a memory-safety bug
or a Proxy escape in one frame is not contained from the rest of the
document's origin. Treat frame isolation as defence-in-depth, not as an
origin sandbox.

The `sandbox` attribute is parsed and enforced, with nested frames
inheriting the intersection of their ancestors' sandboxes:

- No `allow-scripts` (or no `sandbox` allowing it) blocks the frame's
  scripts from running at all.
- No `allow-same-origin` makes the frame opaque-origin: `localStorage`
  and `sessionStorage` throw `SecurityError`, and `document.cookie` reads
  empty and ignores writes.
- No `allow-forms` blocks form submission; no `allow-modals` neutralises
  `alert`/`confirm`/`prompt`/`print`; no `allow-popups` makes
  `window.open` return `null`.

A plain `<iframe>` with no `sandbox` attribute runs its scripts. Cross-
document `postMessage` between a frame and its parent is currently limited
(see Known gaps).

### Cookies and the `document.cookie` surface

Network cookies live in libcurl's per-site cookie jar on disk. They
are parsed, scoped, and re-sent by libcurl, honouring `HttpOnly`,
`Secure`, `SameSite`, `Path`, `Domain`, and expiry. At navigation the
non-`HttpOnly` cookies for the document's origin are read back out of
the jar (`nd_net_cookies_for_js`) and seeded into `document.cookie`,
and the `document.cookie` setter writes back into that same per-site
jar (`nd_net_cookie_store_from_js`) — so a cookie set from JS is sent
on the next request, and a cookie set over the network is visible to a
later `document.cookie` read. `HttpOnly` cookies are written by libcurl
with a `#HttpOnly_` line prefix that the JS read path skips, so they
stay invisible to script.

The `document.cookie` setter:

- Caps input length at 4 KiB.
- Requires a non-empty `name`.
- Maintains a per-tab in-memory mirror for synchronous read-back, then
  persists to the network jar.
- Parses attributes after the first `;`. `Max-Age` (seconds) and
  `Expires` (HTTP-date, via `curl_getdate`) set the jar expiry;
  `Max-Age<=0` or a past `Expires` deletes the named cookie. `Secure`
  is rejected outright from non-HTTPS origins. `Domain` is range-checked
  against the document host before it widens scope; absent, the cookie
  is stored host-only. `Path` defaults to `/`.

## Known gaps

- **Iframe isolation is JS-level, not a runtime or process boundary.**
  A loaded frame shares the parent tab's QuickJS runtime and global
  prototypes; its separate `window`/`document`/`location` and redirected
  `top`/`parent` are a synthetic scope, not a true cross-origin sandbox.
  Cross-document `postMessage` is correspondingly limited. A
  per-origin/per-frame runtime would close this and is tracked as future
  work; until then, do not rely on a cross-origin frame being contained
  from the embedding origin.
- **No per-path filesystem sandbox on Windows.** The mitigation
  suite restricts the *process* (no remote DLL loads, no dynamic
  code, no child processes, etc.) but does not allow-list the
  files the renderer can read or write the way Landlock does on
  Linux. AppContainer or Low-Integrity-Level drop would close
  this; both require additional integration work (manifest /
  capability declarations / re-routed config paths) and are
  tracked as future work.
- **`document.cookie` writes to the jar without file locking.** A
  JS cookie write and a concurrent libcurl jar flush from an
  in-flight transfer are not serialised against each other, so a
  write can occasionally be lost to a racing flush. A shared,
  locked cookie store is the long-term fix; in practice script
  cookie writes happen between transfers, so the window is small.
- **`HttpOnly` name collisions from JS are not rejected.** Setting
  `document.cookie` with the same name as an existing `HttpOnly`
  cookie adds a second entry rather than being refused; the
  `HttpOnly` line is preserved untouched.
