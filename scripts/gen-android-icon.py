#!/usr/bin/env python3
"""Render Nordstjernen's SVG logo into the Android launcher icon set.

Regenerate after editing data/icons/hicolor/scalable/apps/nordstjernen.svg:

    python scripts/gen-android-icon.py

Produces the legacy square/round mipmaps and the adaptive-icon foreground
(full-bleed) so the Android app shows the same N-with-star badge as the GTK
desktop build. Requires rsvg-convert in PATH (MSYS2 librsvg / Debian
librsvg2-bin).
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SVG = ROOT / "data" / "icons" / "hicolor" / "scalable" / "apps" / "nordstjernen.svg"
RES = ROOT / "android" / "app" / "src" / "main" / "res"

# density -> (legacy launcher px, adaptive foreground px @108dp)
DENSITIES = {
    "mdpi":    (48, 108),
    "hdpi":    (72, 162),
    "xhdpi":   (96, 216),
    "xxhdpi":  (144, 324),
    "xxxhdpi": (192, 432),
}


def render(size: int, out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["rsvg-convert", "-w", str(size), "-h", str(size),
         "-f", "png", str(SVG), "-o", str(out)],
        check=True,
    )


def main() -> int:
    if not SVG.is_file():
        print(f"missing source: {SVG}", file=sys.stderr)
        return 1
    for density, (legacy, fg) in DENSITIES.items():
        d = RES / f"mipmap-{density}"
        render(legacy, d / "ic_launcher.png")
        render(legacy, d / "ic_launcher_round.png")
        render(fg, d / "ic_launcher_foreground.png")
        print(f"mipmap-{density}: launcher {legacy}px, foreground {fg}px")
    return 0


if __name__ == "__main__":
    sys.exit(main())
