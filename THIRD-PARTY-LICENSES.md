# Third-party software notices

Nordstjernen links to (and in some cases statically includes) the
following open-source libraries. One of them, ns-pango, is a **modified**
LGPL library; its source and our relink offer are described in its entry
below. Their copyright notices and license
texts are reproduced below. For libraries shipped dynamically in the
release bundles, you are entitled by the LGPL terms to replace them
with modified versions; the binary will continue to function with any
ABI-compatible replacement.

The Nordstjernen source code itself is not open-source. See `README.md`
for the project's own license terms.

---

## Statically linked

### lexbor — Apache License 2.0

> HTML / CSS / WHATWG URL parser.
> <https://github.com/lexbor/lexbor>
>
> Copyright (c) 2018-2025 Alexander Borisov

Licensed under the Apache License, Version 2.0 (the "License"); you may
not use this software except in compliance with the License. You may
obtain a copy of the License at:

  <http://www.apache.org/licenses/LICENSE-2.0>

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing permissions
and limitations under the License.

### Wuffs — Apache License 2.0

> Memory-safe PNG / GIF / BMP / JPEG decoders, transpiled from the
> Wuffs language to C. Vendored as the single-file release in
> `subprojects/wuffs/wuffs-v0.4.c`.
> <https://github.com/google/wuffs-mirror-release-c>
>
> Copyright (c) 2017 The Wuffs Authors.

Licensed under the Apache License, Version 2.0. See the lexbor section
above for the license text (same license).

### quickjs-ng — MIT License

> JavaScript engine.
> <https://github.com/quickjs-ng/quickjs>
>
> Copyright (c) 2017-2026 Fabrice Bellard
> Copyright (c) 2017-2026 Charlie Gordon
> Copyright (c) 2023-2026 the quickjs-ng contributors

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### pl_mpeg — MIT License

> Single-file MPEG-1 video and MP2 audio decoder and MPEG-PS demuxer.
> Vendored at `subprojects/plmpeg/pl_mpeg.h`.
> <https://github.com/phoboslab/pl_mpeg>
>
> Copyright (c) 2019 Dominic Szablewski

Licensed under the MIT License. See the quickjs-ng section above for the
license text (same license).

### minimp3 — CC0 1.0 Universal (public domain dedication)

> Single-file MP3 decoder, used by the `nordstjernen-audio` helper.
> Vendored at `src/audio/minimp3.h`.
> <https://github.com/lieff/minimp3>

To the extent possible under law, the authors have dedicated all
copyright and related and neighboring rights to this software to the
public domain worldwide. This software is distributed without any
warranty. See <http://creativecommons.org/publicdomain/zero/1.0/>.

### WebAssembly Micro Runtime (WAMR) — Apache License 2.0 with LLVM exceptions

> WebAssembly runtime. Vendored in `src/wamr/`.
> <https://github.com/bytecodealliance/wasm-micro-runtime>
>
> Copyright (c) The WebAssembly Micro Runtime contributors.

Licensed under the Apache License, Version 2.0, with LLVM exceptions.
See the lexbor section above for the base Apache 2.0 text; the full
license including the LLVM exceptions is reproduced in
`src/wamr/LICENSE`.

---

### ns-pango — GNU LGPL 2.1 or later (modified Pango, statically linked)

> Text itemization, shaping and line breaking, with a cache of shaped runs
> that Pango itself does not keep.
> <https://github.com/nordstjernen-web/ns-pango>
>
> Copyright the GNU Project and contributors, and Northstar contributors
> for the modifications.

**This is a modified copy of Pango.** It adds a process-wide cache of
shaped glyph strings and of context font metrics, renames every exported
symbol so it can share a process with the system Pango that GTK loads, and
removes the backends and tooling a browser does not use. It remains
licensed under the GNU Lesser General Public License version 2.1 or, at
your option, any later version, and its complete corresponding source --
including every modification -- is published at the URL above. The full
license text is available at:

  <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>

Unlike the other LGPL libraries listed here, ns-pango is linked
**statically** into the Nordstjernen executables on desktop platforms. Per
LGPL section 6(a), you are entitled to modify ns-pango and relink
Nordstjernen against your modified copy: write to us through the contact
details published at <https://nordstjernen.org> (the address `License.md`
gives for licensing inquiries) and we will supply the Nordstjernen object
files, together with any data and utility programs needed, so that you can
produce a modified executable.

Android and iOS builds do not include ns-pango. The Android app ships the
stock Pango as a shared library (see the GTK 4 / GLib / Pango section);
the iOS build, which is not released, links its dependency libraries
statically.

## Dynamically linked

### libcurl — curl license (MIT-like)

> HTTP/TLS client.
> <https://curl.se>
>
> Copyright (c) 1996-2026 Daniel Stenberg, and many contributors.

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE
FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

### OpenSSL (libcrypto) — Apache License 2.0

> Web Cryptography (`crypto.subtle`) primitives and the TLS backend used
> transitively by libcurl.
> <https://www.openssl.org>
>
> Copyright (c) 1998-2026 The OpenSSL Project Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0. See the lexbor section
above for the license text (same license).

### libwebp — BSD 3-Clause License

> WebP image decoding. libwebp's libsharpyuv, which libwebp and libavif
> load, is part of the same project under the same license and ships
> with it in the Android app and the Windows bundle.
> <https://chromium.googlesource.com/webm/libwebp>
>
> Copyright (c) 2010, Google Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met: redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer;
redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution;
neither the name of Google nor the names of its contributors may be used
to endorse or promote products derived from this software without
specific prior written permission. THIS SOFTWARE IS PROVIDED BY THE
COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE.

### libpsl — MIT License

> Public Suffix List handling for cookie / origin policy.
> <https://github.com/rockdaboot/libpsl>
>
> Copyright (c) 2014-2024 Tim Rühsen

Licensed under the MIT License. See the quickjs-ng section above for the
full text (same license).

### SQLite — public domain

> Embedded SQL database used for history, bookmarks, and caches.
> <https://www.sqlite.org>

SQLite is in the public domain. The authors disclaim copyright to the
source code; it may be used for any purpose, commercial or
non-commercial, without restriction.

### libepoxy — MIT License

> OpenGL / OpenGL ES function-pointer management (used by the WebGL
> backend and GTK's GL rendering).
> <https://github.com/anholt/libepoxy>
>
> Copyright (c) 2013-2014 Intel Corporation

Licensed under the MIT License. See the quickjs-ng section above for the
full text (same license).

### zlib — zlib License

> DEFLATE compression used by image decoders and HTTP content decoding.
> <https://zlib.net>
>
> Copyright (c) 1995-2024 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software. Permission is granted to anyone to
use this software for any purpose, including commercial applications, and
to alter it and redistribute it freely, subject to the restrictions in
the zlib license: the origin of this software must not be misrepresented;
altered source versions must be plainly marked as such; and this notice
may not be removed from any source distribution.

### libseccomp — GNU LGPL 2.1 (Linux only)

> Syscall-filter sandbox for the renderer process. Linked only on Linux.
> <https://github.com/seccomp/libseccomp>
>
> Copyright (c) Paul Moore and the libseccomp contributors.

Licensed under the GNU Lesser General Public License version 2.1. See the
LGPL section below for terms and obligations.

### libuchardet — MPL-1.1 / LGPL-2.1+ / GPL-2.0+ (tri-license)

> Charset detection.
> <https://www.freedesktop.org/wiki/Software/uchardet/>
>
> Based on Mozilla's universalchardet, originally Copyright (c)
> 1998-2006 Netscape Communications Corporation and others.

Distributed under the terms of the Mozilla Public License 1.1, the
GNU Lesser General Public License 2.1, or the GNU General Public
License 2.0, at your option. The full license texts are available at:

- <https://www.mozilla.org/MPL/1.1/>
- <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>
- <https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>

### GTK 4, GLib, Pango, gdk-pixbuf — GNU LGPL 2.1 or later

> UI toolkit and core utilities.
> <https://www.gtk.org>, <https://gitlab.gnome.org/GNOME/glib>,
> <https://gitlab.gnome.org/GNOME/pango>,
> <https://gitlab.gnome.org/GNOME/gdk-pixbuf>
>
> Copyright the GNU Project and contributors.

These libraries are licensed under the GNU Lesser General Public
License version 2.1, or (at your option) any later version. The full
license text is available at:

  <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>

Per LGPL section 6, since Nordstjernen links to these libraries
dynamically, you are entitled to modify them and re-link Nordstjernen
against the modified copies. On Windows / macOS bundles the libraries
are shipped alongside the executable as ordinary DLLs / dylibs that you
can replace; on Linux distributions they are loaded from the system
package manager.

The Android app ships GLib (with GObject, GIO and GModule), Pango (with
PangoCairo and PangoFT2), Cairo, FriBidi and proxy-libintl in the same
way: each is a separate shared library (`lib/<abi>/*.so` in the APK)
that `libnordstjernen.so` loads when the app starts, not code compiled
into it. Using a modified, ABI-compatible build of one of them means
putting it in place of the original `.so` in the APK and signing the
rebuilt APK with your own key, because Android installs only signed
packages; a package signed with a different key cannot be installed over
the Play Store app, which has to be uninstalled first.

### FFmpeg — libavformat / libavcodec / libavutil / libswscale / libswresample — GNU LGPL 2.1 or later (inline WebM)

> Container demuxing and audio/video decoding for the inline WebM path
> (VP9/VP8 video, Opus/Vorbis audio). <https://ffmpeg.org>
>
> Copyright the FFmpeg developers.

Required on Linux and Windows, auto-detected on macOS, and absent from
Android builds, so it is present in every build with WebM support. The copy
bundled in the macOS / Windows releases is built **LGPL-only** — its
`configure` uses `--disable-gpl --disable-nonfree --disable-version3
--disable-autodetect`, so it contains no GPL components and no external
codec libraries; the enabled VP8/VP9/Opus/Vorbis decoders are FFmpeg's
own LGPL implementations (see `scripts/build-ffmpeg-lgpl.sh`). The full
license text is available at:

  <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>

Per LGPL section 6, the libraries are linked dynamically: on the
macOS / Windows bundles they ship beside the executable as ordinary
dylibs / DLLs you can replace and re-link against; on Linux they are
loaded from the distribution's FFmpeg packages.

### Cairo — LGPL-2.1 or MPL-1.1

> 2D drawing.
> <https://www.cairographics.org>
>
> Copyright Carl Worth, Behdad Esfahbod, and the Cairo contributors.

Dual-licensed under the GNU Lesser General Public License 2.1 or the
Mozilla Public License 1.1. See:

- <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>
- <https://www.mozilla.org/MPL/1.1/>

### librsvg — GNU LGPL 2.1 or later (Windows / macOS bundles)

> Not used by the browser: SVG is rendered in-engine by `src/svg.c`.
> librsvg reaches the Windows and macOS bundles only as a transitive
> dependency of the gdk-pixbuf loader directory and of GTK's symbolic
> icon loading, so its notice is reproduced here for redistribution.
> <https://gitlab.gnome.org/GNOME/librsvg>
>
> Copyright the GNU Project and contributors.

Licensed under the GNU Lesser General Public License version 2.1 or
later. See the LGPL section above for terms and obligations.

### HarfBuzz — "Old MIT" license

> OpenType text shaping. ns-pango, the system Pango and GTK shape every
> run of text through it, so every build links it; the Windows and macOS
> bundles and the Android app ship it.
> <https://harfbuzz.github.io>
>
> Copyright © 2010-2022  Google, Inc.
> Copyright © 2015-2020  Ebrahim Byagowi
> Copyright © 2019,2020  Facebook, Inc.
> Copyright © 2012,2015  Mozilla Foundation
> Copyright © 2011  Codethink Limited
> Copyright © 2008,2010  Nokia Corporation and/or its subsidiary(-ies)
> Copyright © 2009  Keith Stribley
> Copyright © 2011  Martin Hosken and SIL International
> Copyright © 2007  Chris Wilson
> Copyright © 2005,2006,2020,2021,2022,2023  Behdad Esfahbod
> Copyright © 2004,2007,2008,2009,2010,2013,2021,2022,2023  Red Hat, Inc.
> Copyright © 1998-2005  David Turner and Werner Lemberg
> Copyright © 2016  Igalia S.L.
> Copyright © 2022  Matthias Clasen
> Copyright © 2018,2021  Khaled Hosny
> Copyright © 2018,2019,2020  Adobe, Inc
> Copyright © 2013-2015  Alexei Podtelezhnikov

Permission is hereby granted, without written agreement and without
license or royalty fees, to use, copy, modify, and distribute this
software and its documentation for any purpose, provided that the
above copyright notice and the following two paragraphs appear in
all copies of this software.

IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

### FriBidi and libthai — GNU LGPL 2.1 or later

> FriBidi implements the Unicode bidirectional algorithm; libthai finds
> Thai word breaks. ns-pango and Pango call them: the desktop builds link
> FriBidi directly, and libthai too when the build host has it (ns-pango
> detects it). The Windows and macOS bundles ship whichever of the two
> is linked, and the Android app ships FriBidi.
> <https://github.com/fribidi/fribidi>,
> <https://linux.thai.net/projects/libthai>
>
> FriBidi: Copyright 2001, 2002, 2004, 2005 Behdad Esfahbod; 2004 Sharif
> FarsiWeb, Inc; 1999, 2000, 2017 Dov Grobgeld.
> libthai: Copyright 2001-2021 Theppitak Karoonboonyanan, Pattara
> Kiatisevi, Vuthichai Ampornaramveth, Poonlap Veerathanabutr and Chanop
> Silpa-Anan.

Both are licensed under the GNU Lesser General Public License version
2.1 or (at your option) any later version, and are linked dynamically.
See the GTK 4 / GLib / Pango section above for the license text and your
right to replace them.

### SDL2 — zlib License

> Audio output for the `nordstjernen-audio` helper, which is built
> whenever SDL2 is present (a required library on the desktop). The
> Windows and macOS bundles ship it; on macOS, where Homebrew's SDL2 is
> the sdl2-compat layer, the bundle also carries SDL3, under the same
> license.
> <https://www.libsdl.org>
>
> Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.
3. This notice may not be removed or altered from any source
   distribution.

### brotli — MIT License

> Brotli decompression for HTTP content encoding. libcurl links it, so it
> ships in the Android app and in the Windows and macOS bundles wherever
> the bundled libcurl uses it; the `-Dhttp_backend=nghttp2` build links
> its decoder directly.
> <https://github.com/google/brotli>
>
> Copyright (c) 2009, 2010, 2013-2016 by the Brotli Authors.

Licensed under the MIT License. See the quickjs-ng section above for the
license text (same license).

### nghttp2, ngtcp2 and nghttp3 — MIT License

> HTTP/2 (nghttp2), QUIC (ngtcp2) and HTTP/3 (nghttp3). libcurl links
> nghttp2 for HTTP/2, so it ships in the Android app and in the Windows
> and macOS bundles; the Windows and macOS bundles also carry ngtcp2 and
> nghttp3, which their libcurl uses for HTTP/3. The optional
> `-Dhttp_backend=nghttp2` build links nghttp2 directly, and ngtcp2 and
> nghttp3 too when its HTTP/3 support is detected.
> <https://nghttp2.org>, <https://github.com/ngtcp2/ngtcp2>,
> <https://github.com/ngtcp2/nghttp3>
>
> Copyright (c) 2012, 2014, 2015, 2016 Tatsuhiro Tsujikawa
> Copyright (c) 2012, 2014, 2015, 2016 nghttp2 contributors
> Copyright (c) 2016 ngtcp2 contributors
> Copyright (c) 2019 nghttp3 contributors

Licensed under the MIT License. See the quickjs-ng section above for the
license text (same license).

### libavif and the AV1 libraries it loads — BSD-style and Apache 2.0 licenses

> AVIF image decoding. libavif is optional (`-Davif`, auto-detected on the
> desktop, never on Android or iOS). Where a release carries it, it also
> carries the AV1 codec libraries that libavif was built against: the
> Windows bundle ships libavif, libaom, dav1d, rav1e, SVT-AV1 and libyuv;
> the macOS bundle ships libavif, libaom (with the libvmaf it links) and
> dav1d; the Debian and Ubuntu `.deb` packages bundle libavif with
> libaom, dav1d, libgav1 (and the Abseil libraries libgav1 needs), libyuv
> (and the libjpeg-turbo it links) and, in the Debian package, rav1e and
> SVT-AV1, under `/usr/lib/nordstjernen` (`scripts/pack-deb.sh`).
> <https://github.com/AOMediaCodec/libavif>,
> <https://aomedia.googlesource.com/aom/>,
> <https://code.videolan.org/videolan/dav1d>,
> <https://github.com/xiph/rav1e>,
> <https://gitlab.com/AOMediaCodec/SVT-AV1>,
> <https://chromium.googlesource.com/codecs/libgav1>,
> <https://chromium.googlesource.com/libyuv/libyuv>,
> <https://github.com/abseil/abseil-cpp>,
> <https://github.com/Netflix/vmaf>
>
> libavif: Copyright 2019-2022 Joe Drago.
> libaom: Copyright 2001-2022, Alliance for Open Media.
> dav1d: Copyright 2018-2024, VideoLAN and dav1d authors; 2018-2023, Two
> Orioles, LLC; and the other authors named in its `COPYING`.
> rav1e: Copyright 2001-2016, Alliance for Open Media; 2017-2021, The
> rav1e contributors.
> SVT-AV1: Copyright 2016-2021 Alliance for Open Media; 2018-2022 Intel
> Corporation; 2020 Tencent Corporation; 2019 Netflix, Inc.
> libgav1: Copyright 2019-2020 Google LLC; 2019-2022 The libgav1 Authors.
> libyuv: Copyright 2011 The LibYuv Project Authors.
> Abseil: Copyright 2000-2017 Google Inc.; 2017-2021 The Abseil Authors.
> libvmaf: Copyright (c) 2020 Netflix, Inc.

libavif, libaom, dav1d and rav1e are licensed under the BSD 2-Clause
License:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

SVT-AV1 is licensed under the BSD 3-Clause Clear License:

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the disclaimer
below) provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the distribution.

 3. Neither the name of the Alliance for Open Media nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS LICENSE.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

libgav1 and Abseil are licensed under the Apache License, Version 2.0
(see the lexbor section above for the license text). libyuv is licensed
under the BSD 3-Clause License (see the libwebp section above for the
license text) together with Google's additional grant of patent rights
in its `PATENTS` file.

libvmaf is licensed under the BSD-2-Clause Plus Patent License:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

Subject to the terms and conditions of this license, each copyright
holder and contributor hereby grants to those receiving rights under
this license a perpetual, worldwide, non-exclusive, no-charge, royalty-
free, irrevocable (except for failure to satisfy the conditions of this
license) patent license to make, have made, use, offer to sell, sell,
import, and otherwise transfer this software, where such license applies
only to those patent claims, already acquired or hereafter acquired,
licensable by such copyright holder or contributor that are necessarily
infringed by:

(a) their Contribution(s) (the licensed copyrights of copyright holders
and non-copyrightable additions of contributors, in source or binary
form) alone; or

(b) combination of their Contribution(s) with the work of authorship to
which such Contribution(s) was added by such copyright holder or
contributor, if, at the time the Contribution is added, such addition
causes such combination to be necessarily infringed. The patent license
shall not apply to any other combinations which include the
Contribution.

Except as expressly stated above, no rights or licenses from any
copyright holder or contributor is granted under this license, whether
expressly, by implication, estoppel or otherwise.

DISCLAIMER

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

libaom, rav1e and SVT-AV1 also carry the Alliance for Open Media Patent
License 1.0, which asks that it be reproduced with any binary
distribution of an implementation:

>     Alliance for Open Media Patent License 1.0
>
>     1. License Terms.
>
>     1.1. Patent License. Subject to the terms and conditions of this License, each
>          Licensor, on behalf of itself and successors in interest and assigns,
>          grants Licensee a non-sublicensable, perpetual, worldwide, non-exclusive,
>          no-charge, royalty-free, irrevocable (except as expressly stated in this
>          License) patent license to its Necessary Claims to make, use, sell, offer
>          for sale, import or distribute any Implementation.
>
>     1.2. Conditions.
>
>     1.2.1. Availability. As a condition to the grant of rights to Licensee to make,
>            sell, offer for sale, import or distribute an Implementation under
>            Section 1.1, Licensee must make its Necessary Claims available under
>            this License, and must reproduce this License with any Implementation
>            as follows:
>
>            a. For distribution in source code, by including this License in the
>               root directory of the source code with its Implementation.
>
>            b. For distribution in any other form (including binary, object form,
>               and/or hardware description code (e.g., HDL, RTL, Gate Level Netlist,
>               GDSII, etc.)), by including this License in the documentation, legal
>               notices, and/or other written materials provided with the
>               Implementation.
>
>     1.2.2. Additional Conditions. This license is directly from Licensor to
>            Licensee.  Licensee acknowledges as a condition of benefiting from it
>            that no rights from Licensor are received from suppliers, distributors,
>            or otherwise in connection with this License.
>
>     1.3. Defensive Termination. If any Licensee, its Affiliates, or its agents
>          initiates patent litigation or files, maintains, or voluntarily
>          participates in a lawsuit against another entity or any person asserting
>          that any Implementation infringes Necessary Claims, any patent licenses
>          granted under this License directly to the Licensee are immediately
>          terminated as of the date of the initiation of action unless 1) that suit
>          was in response to a corresponding suit regarding an Implementation first
>          brought against an initiating entity, or 2) that suit was brought to
>          enforce the terms of this License (including intervention in a third-party
>          action by a Licensee).
>
>     1.4. Disclaimers. The Reference Implementation and Specification are provided
>          "AS IS" and without warranty. The entire risk as to implementing or
>          otherwise using the Reference Implementation or Specification is assumed
>          by the implementer and user. Licensor expressly disclaims any warranties
>          (express, implied, or otherwise), including implied warranties of
>          merchantability, non-infringement, fitness for a particular purpose, or
>          title, related to the material. IN NO EVENT WILL LICENSOR BE LIABLE TO
>          ANY OTHER PARTY FOR LOST PROFITS OR ANY FORM OF INDIRECT, SPECIAL,
>          INCIDENTAL, OR CONSEQUENTIAL DAMAGES OF ANY CHARACTER FROM ANY CAUSES OF
>          ACTION OF ANY KIND WITH RESPECT TO THIS LICENSE, WHETHER BASED ON BREACH
>          OF CONTRACT, TORT (INCLUDING NEGLIGENCE), OR OTHERWISE, AND WHETHER OR
>          NOT THE OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
>
>     2. Definitions.
>
>     2.1. Affiliate.  "Affiliate" means an entity that directly or indirectly
>          Controls, is Controlled by, or is under common Control of that party.
>
>     2.2. Control. "Control" means direct or indirect control of more than 50% of
>          the voting power to elect directors of that corporation, or for any other
>          entity, the power to direct management of such entity.
>
>     2.3. Decoder.  "Decoder" means any decoder that conforms fully with all
>          non-optional portions of the Specification.
>
>     2.4. Encoder.  "Encoder" means any encoder that produces a bitstream that can
>          be decoded by a Decoder only to the extent it produces such a bitstream.
>
>     2.5. Final Deliverable.  "Final Deliverable" means the final version of a
>          deliverable approved by the Alliance for Open Media as a Final
>          Deliverable.
>
>     2.6. Implementation.  "Implementation" means any implementation, including the
>          Reference Implementation, that is an Encoder and/or a Decoder. An
>          Implementation also includes components of an Implementation only to the
>          extent they are used as part of an Implementation.
>
>     2.7. License. "License" means this license.
>
>     2.8. Licensee. "Licensee" means any person or entity who exercises patent
>          rights granted under this License.
>
>     2.9. Licensor.  "Licensor" means (i) any Licensee that makes, sells, offers
>          for sale, imports or distributes any Implementation, or (ii) a person
>          or entity that has a licensing obligation to the Implementation as a
>          result of its membership and/or participation in the Alliance for Open
>          Media working group that developed the Specification.
>
>     2.10. Necessary Claims.  "Necessary Claims" means all claims of patents or
>           patent applications, (a) that currently or at any time in the future,
>           are owned or controlled by the Licensor, and (b) (i) would be an
>           Essential Claim as defined by the W3C Policy as of February 5, 2004
>           (https://www.w3.org/Consortium/Patent-Policy-20040205/#def-essential)
>           as if the Specification was a W3C Recommendation; or (ii) are infringed
>           by the Reference Implementation.
>
>     2.11. Reference Implementation. "Reference Implementation" means an Encoder
>           and/or Decoder released by the Alliance for Open Media as a Final
>           Deliverable.
>
>     2.12. Specification. "Specification" means the specification designated by
>           the Alliance for Open Media as a Final Deliverable for which this
>           License was issued.

### libjpeg-turbo — IJG License and BSD 3-Clause License

> JPEG decoding used by libyuv among the `.deb` packages' bundled AV1
> libraries, by libtiff and gdk-pixbuf in the Windows and macOS bundles,
> and by libyuv in the Windows bundle. (Page images are decoded by Wuffs,
> not by libjpeg-turbo.)
> <https://libjpeg-turbo.org>
>
> Copyright (C) 2009-2026 D. R. Commander
> Copyright (C) 2018-2023 Randy <randy408@protonmail.com>

This software is based in part on the work of the Independent JPEG
Group.

libjpeg-turbo's libjpeg API library is covered by the IJG License, and
its TurboJPEG API library and build system by the Modified (3-clause)
BSD License:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
- Neither the name of the libjpeg-turbo Project nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS", AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### PCRE2 — BSD 3-Clause License with the PCRE2 exception

> Regular expressions for GLib (`GRegex`). It ships with GLib in the
> Android app and in the Windows and macOS bundles.
> <https://github.com/PCRE2Project/pcre2>
>
> Copyright (c) 1997-2007 University of Cambridge
> Copyright (c) 2007-2024 Philip Hazel
> Copyright (c) 2009-2024 Zoltan Herczeg (just-in-time compiler)

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notices, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notices, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of the University of Cambridge nor the names of any
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### libpng — PNG Reference Library License version 2

> PNG encoding and decoding for Cairo, FreeType and gdk-pixbuf. It ships
> in the Android app and in the Windows and macOS bundles. (Page images
> are decoded by Wuffs, not by libpng.)
> <http://www.libpng.org/pub/png/libpng.html>
>
> Copyright (c) 1995-2026 The PNG Reference Library Authors.
> Copyright (c) 2018-2026 Cosmin Truta.
> Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.
> Copyright (c) 1996-1997 Andreas Dilger.
> Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

The software is supplied "as is", without warranty of any kind,
express or implied, including, without limitation, the warranties
of merchantability, fitness for a particular purpose, title, and
non-infringement.  In no event shall the Copyright owners, or
anyone distributing the software, be liable for any damages or
other liability, whether in contract, tort or otherwise, arising
from, out of, or in connection with the software, or the use or
other dealings in the software, even if advised of the possibility
of such damage.

Permission is hereby granted to use, copy, modify, and distribute
this software, or portions hereof, for any purpose, without fee,
subject to the following restrictions:

 1. The origin of this software must not be misrepresented; you
    must not claim that you wrote the original software.  If you
    use this software in a product, an acknowledgment in the product
    documentation would be appreciated, but is not required.

 2. Altered source versions must be plainly marked as such, and must
    not be misrepresented as being the original software.

 3. This Copyright notice may not be removed or altered from any
    source or altered source distribution.

### pixman — MIT License

> Pixel manipulation for Cairo. It ships in the Android app and in the
> Windows and macOS bundles.
> <https://www.pixman.org>
>
> Copyright 1987, 1988, 1989, 1998  The Open Group
> Copyright 1987, 1988, 1989 Digital Equipment Corporation
> Copyright 1999, 2004, 2008 Keith Packard
> Copyright 2000 SuSE, Inc.
> Copyright 2000 Keith Packard, member of The XFree86 Project, Inc.
> Copyright 2004, 2005, 2007, 2008, 2009, 2010 Red Hat, Inc.
> Copyright 2004 Nicholas Miell
> Copyright 2005 Lars Knoll & Zack Rusin, Trolltech
> Copyright 2005 Trolltech AS
> Copyright 2007 Luca Barbato
> Copyright 2008 Aaron Plattner, NVIDIA Corporation
> Copyright 2008 Rodrigo Kumpera
> Copyright 2008 André Tupinambá
> Copyright 2008 Mozilla Corporation
> Copyright 2008 Frederic Plourde
> Copyright 2009, Oracle and/or its affiliates. All rights reserved.
> Copyright 2009, 2010 Nokia Corporation

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### Expat and libffi — MIT License

> Expat parses Fontconfig's XML configuration; libffi is GObject's
> foreign-function call layer. Both ship in the Android app and in the
> Windows bundle; macOS provides its own.
> <https://libexpat.github.io>, <https://sourceware.org/libffi/>
>
> Expat: Copyright (C) 1998-2000 Thai Open Source Software Center Ltd
> and Clark Cooper; Copyright (C) 2001-2017 Expat maintainers.
> libffi: Copyright (c) 1996-2024 Red Hat, Inc.; Copyright (C) 1996-2024
> Anthony Green; Copyright (C) 1996-2010 Free Software Foundation, Inc;
> and the other contributors named in its `LICENSE`.

Both are licensed under the MIT License. See the quickjs-ng section above
for the license text (same license).

### proxy-libintl — GNU Library GPL 2.0 or later (Android)

> A stub that stands in for GNU gettext's libintl where the platform has
> none; GLib uses it on Android, where it ships as `libintl.so` in the
> app. It performs no translation of its own.
> <https://github.com/frida/proxy-libintl>
>
> Copyright (C) 2008 Tor Lillqvist

Licensed under the GNU Library General Public License version 2 or (at
your option) any later version, and linked dynamically. The full license
text is available at:

  <https://www.gnu.org/licenses/old-licenses/lgpl-2.0.html>

### libidn2 and libunistring — GNU LGPL 3.0 or later, or GNU GPL 2.0 or later

> Internationalized domain names and the Unicode routines they use. The
> Windows bundle ships both as dependencies of libcurl and libpsl; on
> Linux, libcurl and libpsl load them from the system packages, and the
> Android app does not use them.
> <https://gitlab.com/libidn/libidn2>,
> <https://www.gnu.org/software/libunistring/>
>
> libidn2: Copyright (C) 2011-2014 Simon Josefsson.
> libunistring: Copyright (C) 1995-2022 Free Software Foundation, Inc.

The libraries are dual-licensed: you may use them under the GNU Lesser
General Public License version 3 or (at your option) any later version,
or under the GNU General Public License version 2 or (at your option)
any later version. They are linked dynamically, as separate DLLs. The
full license texts are available at:

- <https://www.gnu.org/licenses/lgpl-3.0.html> (which incorporates
  <https://www.gnu.org/licenses/gpl-3.0.html>)
- <https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>

### Optional dynamic dependencies

These are linked only when present on the build host (meson
`required: false`) or when a build option selects them. When a build
links or bundles them, their notices apply:

- **libavif** — BSD 2-Clause, © 2019-2022 Joe Drago. AVIF image
  decoding; see its section above for the AV1 libraries that come with it.
- **Poppler** (`poppler-glib`) — GNU GPL 2.0 or later, © the Poppler
  developers. PDF rendering. Note: Poppler is GPL; a build that links it
  is subject to the GPL for that combined binary. It is therefore **not**
  part of the official builds or packages, which have no inline PDF viewer:
  the meson `pdf` feature that links it defaults to `disabled`, and it is
  linked only into a private build configured with `-Dpdf=enabled` (or
  `-Dpdf=auto` on a host that has poppler-glib).
- **GnuTLS** — GNU LGPL 2.1 or later, © the Free Software Foundation and
  the GnuTLS contributors. The QUIC TLS stack of the HTTP/3 support in the
  optional `-Dhttp_backend=nghttp2` build (not Windows); the default curl
  build does not link it. GnuTLS's own licensing notes that linking it
  also brings in Nettle, GMP and libunistring, which are dual-licensed
  LGPL-3.0-or-later or GPL-2.0-or-later, so a program linking GnuTLS must
  be available under terms compatible with one of those licenses. Such a
  build loads GnuTLS and its dependencies from the Linux system packages;
  no release bundles them:
  - **Nettle and Hogweed** — GNU LGPL 3.0 or later, or GNU GPL 2.0 or
    later, © 2001-2023 Niels Möller.
  - **GMP** — GNU LGPL 3.0 or later, or GNU GPL 2.0 or later, © the Free
    Software Foundation, Inc.
  - **libtasn1** — GNU LGPL 2.1 or later, © 2000-2022 Free Software
    Foundation, Inc.
  - **p11-kit** — BSD 3-Clause, © 2012-2023 Red Hat Inc. and the other
    contributors named in its `COPYING`.
  - **libidn2 and libunistring** — see their section above.
- **V8** — BSD 3-Clause, © the V8 project authors. The experimental
  `-Djs_engine=v8` backend, linked statically as a V8 monolith; not part
  of the official builds. V8's `LICENSE` names the externally maintained
  code it carries under other licenses, and a monolith also includes the
  third-party libraries of the V8 checkout it was built from, whose
  `LICENSE` files are in that checkout's sub-directories.
- **Fontconfig** — MIT-style license, © Keith Packard and contributors.
  Font discovery; ns-pango requires it on the desktop, and it ships in the
  Android app and in the Windows and macOS bundles.
- **FreeType** — FreeType License (BSD-style with credit clause) or GNU
  GPL 2.0, at your option, © The FreeType Project. Font rasterization;
  ns-pango requires it on the desktop, and it ships in the Android app and
  in the Windows and macOS bundles. Under the FreeType License:
  Portions of this software are copyright © 1996-2026 The FreeType
  Project (www.freetype.org). All rights reserved.
- **wgpu-native** — MIT or Apache 2.0, © the gfx-rs authors. The
  experimental WebGPU backend. Its two C headers are vendored under
  `third_party/wgpu-native/include/webgpu/`: `webgpu.h` is BSD 3-Clause,
  © 2019-2023 the WebGPU-Native developers; `wgpu.h` carries
  wgpu-native's own MIT-or-Apache-2.0 terms. The library itself is never
  vendored — it is located at build time and, in the release bundles,
  ships beside the executable.

---


## NOTICE files

Apache 2.0 section 4(d) requires propagating any `NOTICE` files
shipped with the upstream sources. As of this release:

- lexbor ships a `NOTICE` file, carried in the fork at
  `src/lexbor/NOTICE` and reproduced verbatim here:

>     Lexbor.
>
>     Copyright 2018-2020 Alexander Borisov
>
>     Licensed under the Apache License, Version 2.0 (the "License");
>     you may not use this file except in compliance with the License.
>     You may obtain a copy of the License at
>
>         http://www.apache.org/licenses/LICENSE-2.0
>
>     Unless required by applicable law or agreed to in writing, software
>     distributed under the License is distributed on an "AS IS" BASIS,
>     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
>     See the License for the specific language governing permissions and
>     limitations under the License.

- Wuffs ships no `NOTICE` file.
- WAMR ships no `NOTICE` file.

If a future upstream release adds one, it will be included verbatim
in this section.
