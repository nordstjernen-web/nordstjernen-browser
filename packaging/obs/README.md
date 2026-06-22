# openSUSE / OBS packaging

Files to upload to the OBS package
`home:andreasrosdal/Nordstjernen`:

- `nordstjernen.spec` — RPM build recipe (builds from source with meson).
- `_service` — pulls the source tarball from the public GitHub repo and
  compresses it to `nordstjernen-<version>.tar.zst`.

## License caveat — read first

Nordstjernen is under the **Nordstjernen Source License v1.0 (NSL-1.0)**,
a source-available license that forbids "Competing Use" and restricts some
purposes to non-commercial use. It is **not** OSI-approved / free software,
so it is tagged `SUSE-NonFree`.

Consequence: this package can live in a **home: project** (or another
non-free repository), but it will **not** be accepted into
**openSUSE:Factory / Tumbleweed**, whose legal review only admits free
licenses. Plan for the home-project repo as the distribution channel
(users add it and `zypper install nordstjernen`). NSL-1.0 converts to MIT
ten years after each release; only then does the Factory route open.

## Build options

The spec disables three meson features that are unwanted or unbuildable in
the OBS sandbox:

- `-Dai=disabled` — the local llama.cpp chat start page pulls a git
  subproject (no network in the build root) and is large; off for packaging.
- `-Dqt=disabled` — experimental Qt 6 frontend.
- `-Dwebgpu=disabled` — needs external wgpu-native, not packaged.

Everything else (GTK shell, sandboxed renderer, SDL2 audio helper, WebGL,
spell-checking via enchant) builds from the declared `BuildRequires`.

## Uploading via the web UI

1. On the package page, **Trigger services** (runs `_service`) once the
   `_service` file is uploaded, or upload a tarball manually.
2. Add `nordstjernen.spec` and `_service`.
3. OBS builds for the enabled repositories/arches; watch the build log and
   iterate.

## Uploading with osc (recommended)

    osc checkout home:andreasrosdal Nordstjernen
    cd home:andreasrosdal/Nordstjernen
    cp .../packaging/obs/nordstjernen.spec .
    cp .../packaging/obs/_service .
    osc service manualrun          # produces the source tarball locally
    osc addremove
    osc commit -m "Initial Nordstjernen package"

Then add build targets in the project's **Repositories** tab (e.g.
openSUSE Tumbleweed, Leap 15.6) and watch the results.

## Versioning

The repo currently reports `1.14-dev` and has no git tags. The spec and
`_service` hard-code `1.14`. Cut real release tags (e.g. `v1.14`) and set
`<param name="versionformat">@PARENT_TAG@</param>` once tags exist so the
version tracks releases automatically.
