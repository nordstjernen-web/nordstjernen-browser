# build-splash-gif.py — animates the comic about:start splash and embeds it as an indexed GIF.
from __future__ import annotations

import base64
from concurrent.futures import ProcessPoolExecutor
import os
from pathlib import Path
import textwrap

from PIL import Image

import importlib.util
import sys

ROOT = Path(__file__).resolve().parent.parent
FRAME_COUNT = int(os.environ.get("NS_SPLASH_FRAMES", "36"))
FRAME_DELAY = int(os.environ.get("NS_SPLASH_DELAY", "70"))
BOIL_EVERY = 3

spec = importlib.util.spec_from_file_location("splash_art", ROOT / "scripts" / "build-splash-art.py")
art = importlib.util.module_from_spec(spec)
sys.modules["splash_art"] = art
spec.loader.exec_module(art)


def render(index: int) -> Image.Image:
    version = art.project_version()
    return art.draw_frame(version, index / FRAME_COUNT, index // BOIL_EVERY)


def indexed_frames(frames: list[Image.Image]) -> list[Image.Image]:
    w, h = frames[0].size
    sample = Image.new("RGB", (w, h * 3))
    for k, index in enumerate((0, FRAME_COUNT // 3, 2 * FRAME_COUNT // 3)):
        sample.paste(frames[index % len(frames)], (0, k * h))
    palette = sample.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    return [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]


def write_header(gif_path: Path) -> None:
    encoded = base64.b64encode(gif_path.read_bytes()).decode("ascii")
    lines = textwrap.wrap(encoded, 96)
    output = [
        "/* about_splash_gif.h — the animated 256-colour about:start splash, embedded. */",
        "#ifndef NS_ABOUT_SPLASH_GIF_H",
        "#define NS_ABOUT_SPLASH_GIF_H",
        "",
        "static const char about_splash_gif_b64[] =",
    ]
    output.extend(
        f'    "{line}"{";" if number == len(lines) - 1 else ""}'
        for number, line in enumerate(lines)
    )
    output.extend(["", "#endif", ""])
    (ROOT / "src" / "about_splash_gif.h").write_text(
        "\n".join(output), encoding="utf-8", newline="\n"
    )


def main() -> None:
    with ProcessPoolExecutor() as pool:
        frames = list(pool.map(render, range(FRAME_COUNT)))
    output = ROOT / "data" / "about-splash.gif"
    indexed = indexed_frames(frames)
    indexed[0].save(
        output,
        save_all=True,
        append_images=indexed[1:],
        duration=FRAME_DELAY,
        loop=0,
        disposal=1,
        optimize=True,
    )
    write_header(output)
    print(f"wrote {output} ({len(indexed)} frames, {output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
