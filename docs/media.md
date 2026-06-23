# Media: video and audio

How Nordstjernen plays `<video>` and `<audio>`. The engine ships a tiny,
in-tree decoder set plus one optional WebM extension; anything else is handed
to an external player.

## Inline video

### MPEG-1 (always on)

`<video>` plays **inline** when the source is MPEG-1 (an `.mpg` / `.mpeg` /
`.m1v` stream). The bytes are decoded entirely in-tree by
[pl_mpeg](https://github.com/phoboslab/pl_mpeg) — a single-file, MIT-licensed
MPEG-1 video decoder — inside the sandboxed renderer process; no external
library, no GPU API, and no syscalls beyond memory are needed, so it runs
comfortably under the seccomp filter. Decoded BGRA frames are advanced off the
renderer's existing animation tick, honouring the `autoplay`, `loop`, `muted`,
`width`/`height`, and `poster` attributes; a click toggles play/pause; and the
`HTMLMediaElement` events (`loadedmetadata`, `durationchange`, `canplay`,
`timeupdate`, `play`, `pause`, `ended`) fire on the element as it plays. MPEG-1
is the one always-on codec, by design — small, patent-free, and decoded in pure
portable C.

### WebM — VP9/VP8 (optional)

`.webm` plays inline too when **FFmpeg's libav\*** (`libavformat` /
`libavcodec` / `libavutil` / `libswscale` / `libswresample`) is present at build
time (auto-detected; `-DNS_HAVE_LIBAV`). libav demuxes the Matroska container
and decodes **VP9/VP8** frames, which are scaled to BGRA and served through the
same off-tick frame loop as the MPEG-1 path — both royalty-free codecs. A build
without the FFmpeg libraries carries no libav symbol or dependency and falls
back to the external-player path.

How libav is obtained differs by platform, to keep it redistributable:

- **Linux** depends on the distribution's FFmpeg at runtime (never bundled), so
  no licensing obligation falls on the package.
- **macOS / Windows** vendor a **minimal, LGPL-only FFmpeg** built from source
  (`scripts/build-ffmpeg-lgpl.sh`), carrying just the matroska/ogg demuxers and
  the native VP8/VP9/Opus/Vorbis decoders — no GPL parts, no external codec
  libraries.
- **Android** stays WebM-free (its NDK dependency sysroot does not cross-build
  FFmpeg).

## Audio

The MPEG-1 stream's **MP2 audio track** plays too (unless the element is
`muted`), as does a WebM's **Opus/Vorbis** track when libav is built in. The
seccomp-sandboxed renderer can't open a sound device, so audio is handed to the
unsandboxed `nordstjernen-audio` helper. The helper decodes to PCM — pl_mpeg for
the MPEG-1/MP2 track, [minimp3](https://github.com/lieff/minimp3) (CC0, vendored)
for standalone `.mp3` files, and libav for Opus/Vorbis (WebM/Ogg) — and plays it
through [SDL2](https://www.libsdl.org/)'s audio device (WASAPI on Windows,
CoreAudio on macOS, ALSA/PulseAudio on Linux), mixing and resampling the streams
itself. The inline player drives the helper — `open`/`play`/`pause`/`seek`/`stop`
ride the renderer→shell render channel, and looping re-syncs the audio at each
wrap.

## Other media → external player

Beyond MPEG-1/MP2, MP3, and the optional WebM (VP9/VP8 + Opus/Vorbis) path,
Nordstjernen ships no media codecs. Other `<audio>` and other `<video>` codecs
render a poster and a play overlay; clicking it resolves the source URL inside
the sandboxed renderer process and the UI shell hands it to an external player —
`mpv`, `VLC`, `celluloid`, `totem`, `mplayer` or `ffplay` on Linux, otherwise
the desktop's default handler for the media type (found via `GAppInfo`, so
Flatpak players work too), the default app via `open` on macOS, and the
registered handler on Windows. If none is found, a status-bar hint suggests
installing [mpv](https://mpv.io). A media player is therefore a *recommended
runtime dependency*, not a build dependency: the `.deb` and `.rpm` packages
`Recommend` one (defaulting to `mpv`) so playback works out of the box, while
source builds need none. The player is launched from the UI shell, never from
the page's untrusted renderer.

Streaming sites (YouTube and friends) drive `<video>` through MSE/`blob:` with no
plain file URL. For those, clicking hands the **page URL** to the player instead,
so `mpv`/`VLC` resolve it with [yt-dlp](https://github.com/yt-dlp/yt-dlp) —
install yt-dlp alongside the player to watch them.
