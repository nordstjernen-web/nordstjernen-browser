# openSUSE / OBS packaging

This directory is the OBS package container for
`home:andreasrosdal/Nordstjernen`:

- `nordstjernen.spec` — RPM build recipe (builds from source with meson).
- `_service` — pulls the source tarball from the public GitHub repo.

## Git-backed package (scmsync) — recommended

The package is bound to this git repo via `scmsync`, so OBS pulls everything
from GitHub and stores no tarball of its own. The package container is this
`packaging/obs/` subdirectory; the `_service` (`tar_scm`) fetches the full
source tree from the same repo at build time.

Set it once on the package meta (`osc meta pkg home:andreasrosdal Nordstjernen
-e`), adding inside `<package>`:

    <scmsync>https://github.com/nordstjernen-web/nordstjernen?trackingbranch=main&amp;subdir=packaging/obs</scmsync>

(`&` must be written `&amp;` in the meta XML.) After this, sources are
authoritative in git — edit here and push, do not edit files in OBS. OBS
tracks `main` and rebuilds when it advances; for instant rebuilds add a git
webhook with an `osc token --create --operation runservice` token.

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

## "Excluded" status

A package shows **excluded** when OBS skips it before building:

- **Only some arches excluded** (i586, ppc64, armv7…): expected — GTK 4 is
  not built there. Keep x86_64 (and aarch64) enabled; ignore the rest.
- **All arches excluded**: OBS has no usable source. Click the "Excluded"
  link for the exact reason. The usual cause is a source service that never
  ran (e.g. an `obs_scm` left in `mode="manual"`, which the web-UI "Trigger
  Services" does not run). Fix by either removing `mode="manual"` so the
  server runs it, or by dropping `_service` and uploading a plain source
  tarball (simplest, deterministic).

## Uploading via the web UI (plain tarball — simplest)

1. Generate the source tarball locally:
   `git archive --format=tar --prefix=nordstjernen-1.14/ HEAD | gzip -9 > nordstjernen-1.14.tar.gz`
2. On the package page, **Add file** → upload `nordstjernen.spec` and
   `nordstjernen-1.14.tar.gz`. Do **not** also keep a `_service`, or the
   service output will fight the uploaded tarball.
3. OBS builds for the enabled repositories/arches; watch the build log.

## Uploading via the web UI (auto-pull from git — recommended)

The `_service` here uses `tar_scm` + `recompress` + `set_version` with **no
`mode="buildtime"`**, so the whole chain runs on the OBS source server and
commits a finished `nordstjernen-1.14.tar.gz` directly. Avoid the
`obs_scm` + buildtime-`tar` split: it produces a `.obscpio`/`.obsinfo`
archive that the build VM must reconstruct, and if the git fetch did not
commit that archive the build dies with
`ERROR: no .obsinfo file found in directory` / `service run failed for
service 'tar'`.

1. Upload (or paste) `nordstjernen.spec` and `_service`, then **Trigger
   Services**.
2. `tar_scm` clones `main`, `recompress` gzips it, `set_version` syncs the
   spec — the committed `nordstjernen-1.14.tar.gz` matches `Source0`.
3. If your OBS instance does not run scm services server-side, run them
   once locally instead: `osc service manualrun` then `osc ci`.

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
