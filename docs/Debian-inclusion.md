# Debian inclusion tracker

Status of getting the `nordstjernen-browser` source package into Debian `main`
through sponsorship on [mentors.debian.net](https://mentors.debian.net/package/nordstjernen/).
`Debian.md` describes the packaging itself; this file records where the
review stands and what is still to do. Update it after each upload and
each review.

## Uploads

| Upload | Version | Date | Result |
|---|---|---|---|
| #1 | `1.0.35+ds-1` | 2026-10-20 | Needs work (two reviews, 2026-10-21) |

## Open

- [ ] File the ITP bug against `wnpp`
      (`reportbug wnpp`, title `ITP: nordstjernen-browser -- web browser`).
- [ ] Put the ITP number in the single `debian/changelog` entry:
      `* Initial release (Closes: #NNNNNNN)`, distribution `unstable`.
- [ ] Upload the current `debian/` tree as upload #2. The second reviewer
      asked to see it before reviewing further (upload #1 predates the
      format 5 `debian/watch`).
- [ ] File the RFS (Request For Sponsorship) bug against `sponsorship-requests`
      once upload #2 is clean.
- [ ] Decide whether to move the packaging to a separate salsa.debian.org
      repository (gbp branches `upstream/latest`, `pristine-tar`,
      `debian/latest`, plus `debian/salsa-ci.yml`) and point `Vcs-*` there.
      Recommended by the first reviewer, not required.
- [ ] `debian-watch-does-not-check-openpgp-signature` (lintian X): GitHub
      tarballs are unsigned; either sign release tags/tarballs or state in
      `debian/watch` that there is no signature to check.
- [ ] `no-manual-page` (lintian W, seen in a local binary build): ship man
      pages for `nordstjernen`, `nordstjernen-renderer` and
      `nordstjernen-audio`.
- [ ] `very-long-line-length-in-source-file` (lintian X, vendored and
      generated files): ignore or add `debian/source/lintian-overrides`.

## Done

- [x] Source and binary package renamed `nordstjernen` → `nordstjernen-browser`
      so the archive name says it is a browser. Upload #1 on mentors is under
      the old name; upload #2 appears as a new package there.

- [x] `debian/copyright` covers the AppStream metainfo under CC0-1.0
      (`inconsistent-appstream-metadata-license`).
- [x] Standards-Version 4.7.4; default `Priority: optional` dropped.
- [x] `debian/watch` moved to format 5 with the GitHub template and
      `Repack-Suffix: +ds` (`prefer-uscan-symlink`).
- [x] `debian/changelog` collapsed to one `Initial release` entry, as a new
      package should have.
- [x] `debian/rules` uses `execute_after_dh_auto_install` instead of an
      `override_dh_auto_install` that re-ran `dh_auto_install`, and removes
      the unshipped files with plain `rm` so a moved file fails the build.
- [x] `Rules-Requires-Root: no` dropped from `debian/control` (the default).
- [x] `debhelper-compat (= 14)`; binary build verified in a `debian:unstable`
      container (debhelper 14.5).

## Review log

- **2026-10-21, Norwid Behrnd — Needs work.** Package unknown to Debian:
  file an ITP and close it from an `Initial release` changelog entry.
  Suggests keeping the packaging in a separate salsa repository rather
  than in the upstream tree.
- **2026-10-21, Jérémy Lal — Needs work.** Use `execute_after_dh_auto_install`;
  no `rm -f`; drop `Rules-Requires-Root: no`; move to debhelper 14; upload
  the latest packaging before further review.
