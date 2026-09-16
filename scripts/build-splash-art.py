# build-splash-art.py — paints the versioned about:start splash as an impressionist night piece.
from __future__ import annotations

import math
from pathlib import Path
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parent.parent
WIDTH = 940
HEIGHT = 320
SS = 2
SEED = 20260916

EARTH_CX = 470.0
EARTH_TOP = 212.0
EARTH_R = 2400.0
STAR_X = 750.0
STAR_Y = 112.0
FRAME = 11

TITLE_FONTS = (
    "C:/Windows/Fonts/BOOKOSB.TTF",
    "C:/Windows/Fonts/cambriab.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSerif-Bold.ttf",
    "/Library/Fonts/DejaVuSerif-Bold.ttf",
    "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
)

IVORY = np.array((246, 236, 212), np.float32)
GOLD_TOP = np.array((250, 200, 78), np.float32)
GOLD_BOTTOM = np.array((214, 142, 18), np.float32)
UMBER = np.array((58, 34, 18), np.float32)


def project_version() -> str:
    text = (ROOT / "meson.build").read_text(encoding="utf-8")
    match = re.search(r"^\s*version:\s*'([^']+)'", text, re.MULTILINE)
    if match is None:
        raise RuntimeError("could not read version from meson.build")
    return match.group(1).split("-", 1)[0]


def load_font(size: int) -> ImageFont.FreeTypeFont:
    for path in TITLE_FONTS:
        if Path(path).is_file():
            return ImageFont.truetype(path, size)
    raise FileNotFoundError("no bold serif font found for the splash title")


def fractal_noise(width: int, height: int, octaves: int, rng, cells: int = 4) -> np.ndarray:
    total = np.zeros((height, width), np.float32)
    amplitude = 1.0
    norm = 0.0
    for octave in range(octaves):
        grid_w = max(2, cells * (2 ** octave))
        grid_h = max(2, int(round(grid_w * height / width)))
        grid = rng.random((grid_h, grid_w)).astype(np.float32)
        scaled = Image.fromarray(grid * 255.0).resize((width, height), Image.Resampling.BICUBIC)
        total += amplitude * (np.asarray(scaled, np.float32) / 255.0)
        norm += amplitude
        amplitude *= 0.5
    return total / norm


def blur_rgb(layer: np.ndarray, radius: float) -> np.ndarray:
    peak = float(layer.max())
    if peak <= 0.0:
        return np.zeros_like(layer)
    scaled = np.clip(layer / peak * 255.0, 0, 255).astype(np.uint8)
    blurred = Image.fromarray(scaled, "RGB").filter(ImageFilter.GaussianBlur(radius))
    return np.asarray(blurred, np.float32) / 255.0 * peak


def limb_y(x: np.ndarray) -> np.ndarray:
    cx = EARTH_CX * SS
    cy = (EARTH_TOP + EARTH_R) * SS
    radius = EARTH_R * SS
    return cy - np.sqrt(np.clip(radius ** 2 - (x - cx) ** 2, 0.0, None))


def lerp(a, b, t):
    return a + (b - a) * t


def underpainting(width: int, height: int, rng) -> np.ndarray:
    yy, xx = np.mgrid[0:height, 0:width].astype(np.float32)
    horizon = limb_y(xx)
    t = np.clip(yy / horizon, 0.0, 1.0)[:, :, None]
    zenith = np.array((14, 12, 46), np.float32)
    dusk = np.array((36, 44, 112), np.float32)
    glow = np.array((92, 120, 176), np.float32)
    sky = lerp(zenith, dusk, t ** 1.6)
    sky = lerp(sky, glow, np.clip((t - 0.72) / 0.28, 0, 1) ** 2.2)
    warm = fractal_noise(width, height, 4, rng, cells=3)[:, :, None]
    sky = lerp(sky, np.array((98, 58, 36), np.float32), 0.16 * warm ** 2)
    violet = fractal_noise(width, height, 3, rng, cells=2)[:, :, None]
    sky = lerp(sky, np.array((84, 46, 128), np.float32), 0.28 * violet ** 1.5)
    teal = fractal_noise(width, height, 3, rng, cells=3)[:, :, None]
    sky = lerp(sky, np.array((28, 96, 122), np.float32), 0.22 * teal ** 1.8)
    below = yy >= horizon
    depth = np.clip((yy - horizon) / (HEIGHT * SS * 0.42), 0, 1)[:, :, None]
    ground = lerp(np.array((10, 26, 62), np.float32), np.array((3, 5, 16), np.float32), depth ** 0.7)
    land = fractal_noise(width, height, 5, rng, cells=5)
    land_mask = np.clip((land - 0.47) * 9.0, 0, 1)[:, :, None]
    ground = lerp(ground, np.array((18, 14, 12), np.float32), land_mask * 0.9)
    canvas = np.where(below[:, :, None], ground, sky)
    return canvas / 255.0


def flow_angle(x: float, y: float, noise, width: int, height: int) -> float:
    sx, sy = STAR_X * SS, STAR_Y * SS
    dx, dy = x - sx, y - sy
    swirl = math.atan2(dy, dx) + math.pi / 2.0
    dist = math.hypot(dx, dy) / (width * 0.55)
    horizon = float(limb_y(np.array([x], np.float32))[0])
    near = 1.0 - min(1.0, max(0.0, (horizon - y) / (HEIGHT * SS * 0.45)))
    angle = swirl * (1.0 - min(1.0, dist)) * (1.0 - near) + 0.0 * near
    wobble = (noise[min(height - 1, max(0, int(y))), min(width - 1, max(0, int(x)))] - 0.5) * 2.4
    return angle + wobble


def dab(draw: ImageDraw.ImageDraw, x: float, y: float, angle: float, length: float,
        width: float, colour) -> None:
    ca, sa = math.cos(angle), math.sin(angle)
    points = []
    for k in range(10):
        a = 2.0 * math.pi * k / 10
        px, py = math.cos(a) * length * 0.5, math.sin(a) * width * 0.5
        points.append((x + px * ca - py * sa, y + px * sa + py * ca))
    draw.polygon(points, fill=tuple(int(c) for c in colour))


def jitter(colour: np.ndarray, rng, spread: float) -> np.ndarray:
    shift = rng.normal(0.0, spread * 0.5, 3).astype(np.float32)
    tint = rng.normal(0.0, spread * 0.6)
    temperature = rng.normal(0.0, spread) * np.array((1.0, 0.25, -1.0), np.float32)
    return np.clip(colour + shift + tint + temperature, 0, 255)


def brush_passes(base: np.ndarray, rng) -> Image.Image:
    height, width = base.shape[:2]
    image = Image.fromarray(np.clip(base * 255.0, 0, 255).astype(np.uint8), "RGB")
    draw = ImageDraw.Draw(image)
    noise = fractal_noise(width, height, 3, rng, cells=6)
    source = base * 255.0
    passes = (
        (9000, 34.0, 12.0, 34.0),
        (20000, 21.0, 7.0, 20.0),
        (14000, 14.0, 4.5, 14.0),
    )
    for count, length, thickness, spread in passes:
        for _ in range(count):
            x = rng.uniform(0, width)
            y = rng.uniform(0, height)
            colour = source[min(height - 1, int(y)), min(width - 1, int(x))]
            angle = flow_angle(x, y, noise, width, height)
            size = length * rng.uniform(0.6, 1.4)
            dab(draw, x, y, angle, size, thickness * rng.uniform(0.7, 1.3),
                jitter(colour, rng, spread))
    return image


def paint_aurora(image: Image.Image, rng) -> None:
    layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    palette = ((44, 226, 178), (112, 240, 150), (150, 96, 224), (66, 200, 232))
    for _ in range(3600):
        x = rng.uniform(20 * SS, 890 * SS)
        band = 0.5 + 0.5 * math.sin(x / (61.0 * SS) + 0.7) * math.sin(x / (23.0 * SS))
        top = float(limb_y(np.array([x], np.float32))[0]) - (28 + 62 * band) * SS
        y = rng.uniform(top, top + (40 + 30 * band) * SS)
        colour = palette[rng.integers(len(palette))]
        alpha = int(30 + 100 * band * rng.uniform(0.4, 1.0))
        length = rng.uniform(10, 30) * SS
        dab(draw, x, y, math.pi / 2 + rng.normal(0, 0.09), length, rng.uniform(2, 4) * SS,
            colour + (alpha,))
    layer = layer.filter(ImageFilter.GaussianBlur(2.0 * SS))
    image.alpha_composite(layer)


def paint_horizon(image: Image.Image, rng) -> None:
    draw = ImageDraw.Draw(image)
    width = image.width
    for _ in range(1400):
        x = rng.uniform(0, width)
        y = float(limb_y(np.array([x], np.float32))[0])
        along = math.atan2(-(x - EARTH_CX * SS), EARTH_R * SS)
        t = rng.random()
        colour = lerp(np.array((120, 200, 255), np.float32), np.array((250, 240, 200), np.float32), t ** 3)
        dab(draw, x, y + rng.normal(0, 1.4) * SS, along, rng.uniform(6, 16) * SS,
            rng.uniform(1.2, 2.4) * SS, jitter(colour, rng, 10.0))
    for _ in range(2200):
        x = rng.uniform(0, width)
        y = float(limb_y(np.array([x], np.float32))[0]) + rng.uniform(2, 34) * SS
        t = rng.random()
        colour = lerp(np.array((30, 70, 150), np.float32), np.array((6, 10, 30), np.float32), t)
        dab(draw, x, y, rng.normal(0, 0.08), rng.uniform(16, 40) * SS, rng.uniform(2.5, 5) * SS,
            jitter(colour, rng, 5.0))


def city_lights(rng, width: int, height: int) -> np.ndarray:
    layer = Image.new("RGB", (width, height), (0, 0, 0))
    draw = ImageDraw.Draw(layer)
    clusters = [(238, 278), (286, 287), (313, 271), (357, 282), (392, 270), (528, 286),
                (555, 275), (617, 289), (678, 275), (705, 291), (738, 282), (770, 299),
                (180, 300), (450, 305), (620, 312), (830, 306)]
    for cx, cy in clusters:
        count = rng.integers(22, 60)
        for _ in range(count):
            r = abs(rng.normal(0, 14))
            a = rng.uniform(0, 2 * math.pi)
            x = (cx + math.cos(a) * r * 1.8) * SS
            y = (cy + math.sin(a) * r * 0.45) * SS
            if y < float(limb_y(np.array([x], np.float32))[0]) + 4 * SS:
                continue
            size = rng.uniform(0.45, 1.2) * SS
            colour = (int(rng.uniform(225, 255)), int(rng.uniform(160, 215)), int(rng.uniform(50, 120)))
            draw.ellipse((x - size, y - size, x + size, y + size), fill=colour)
        for _ in range(rng.integers(2, 5)):
            a = rng.uniform(0, 2 * math.pi)
            far = rng.uniform(30, 90)
            draw.line(((cx * SS, cy * SS), ((cx + math.cos(a) * far) * SS, (cy + math.sin(a) * far * 0.35) * SS)),
                      fill=(150, 110, 50), width=1)
    return np.asarray(layer, np.float32) / 255.0


def star_field(rng, width: int, height: int) -> np.ndarray:
    layer = Image.new("RGB", (width, height), (0, 0, 0))
    draw = ImageDraw.Draw(layer)
    for _ in range(620):
        x = rng.uniform(0, width)
        y = rng.uniform(0, height)
        if y > float(limb_y(np.array([x], np.float32))[0]) - 30 * SS:
            continue
        size = rng.uniform(0.35, 1.1) * SS * (1.0 if rng.random() > 0.04 else 2.0)
        warmth = rng.random()
        level = rng.uniform(0.55, 1.0)
        colour = (int(255 * level), int((228 + 27 * warmth) * level), int((186 + 69 * (1 - warmth)) * level))
        draw.ellipse((x - size, y - size, x + size, y + size), fill=colour)
    return np.asarray(layer, np.float32) / 255.0


def north_star(width: int, height: int) -> np.ndarray:
    layer = Image.new("RGB", (width, height), (0, 0, 0))
    draw = ImageDraw.Draw(layer)
    cx, cy = STAR_X * SS, STAR_Y * SS
    for reach, thick, colour in ((78, 3.2, (255, 244, 205)), (34, 5.0, (255, 250, 230))):
        r = reach * SS
        w = max(1, int(thick * SS))
        draw.line((cx - r, cy, cx + r, cy), fill=colour, width=w)
        draw.line((cx, cy - r, cx, cy + r), fill=colour, width=w)
        d = r * 0.42
        draw.line((cx - d, cy - d, cx + d, cy + d), fill=colour, width=max(1, w // 2))
        draw.line((cx - d, cy + d, cx + d, cy - d), fill=colour, width=max(1, w // 2))
    core = 7 * SS
    draw.ellipse((cx - core, cy - core, cx + core, cy + core), fill=(255, 255, 248))
    rays = np.asarray(layer, np.float32) / 255.0
    halo = np.zeros_like(rays)
    yy, xx = np.mgrid[0:height, 0:width].astype(np.float32)
    dist = np.hypot(xx - cx, yy - cy) / (SS * 150.0)
    halo[:, :, 0] = np.exp(-dist * 2.2) * 0.75
    halo[:, :, 1] = np.exp(-dist * 2.4) * 0.66
    halo[:, :, 2] = np.exp(-dist * 2.8) * 0.42
    return blur_rgb(rays, 1.0 * SS) + blur_rgb(rays, 6.0 * SS) * 0.8 + halo


def gold_text(image: Image.Image, text: str, position, font, top, bottom, shadow=(8, 4, 2)) -> int:
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).text(position, text, font=font, fill=255)
    box = mask.getbbox()
    ink = np.asarray(mask, np.float32)[:, :, None] / 255.0
    pixels = np.asarray(image.convert("RGB"), np.float32)
    shadow_mask = np.asarray(mask.filter(ImageFilter.GaussianBlur(2.2 * SS)), np.float32)[:, :, None] / 255.0
    offset = np.roll(np.roll(shadow_mask, 3 * SS, axis=0), 2 * SS, axis=1)
    pixels = lerp(pixels, np.array(shadow, np.float32), offset * 0.85)
    ramp = np.linspace(0.0, 1.0, image.height, dtype=np.float32)
    y0, y1 = (box[1], box[3]) if box else (0, image.height)
    ramp = np.clip((np.arange(image.height, dtype=np.float32) - y0) / max(1, y1 - y0), 0, 1)
    fill = lerp(np.asarray(top, np.float32)[None, None, :], np.asarray(bottom, np.float32)[None, None, :], ramp[:, None, None])
    pixels = lerp(pixels, fill, ink)
    image.paste(Image.fromarray(np.clip(pixels, 0, 255).astype(np.uint8), "RGB"))
    return box[2] if box else position[0]


def paint_title(image: Image.Image, version: str) -> None:
    title_font = load_font(int(60 * SS))
    tag_font = load_font(int(26 * SS))
    x = 66 * SS
    end = gold_text(image, "Nordstjernen", (x, 38 * SS), title_font, IVORY, (228, 206, 168))
    gold_text(image, version, (end + 16 * SS, 38 * SS), title_font, GOLD_TOP, GOLD_BOTTOM)
    gold_text(image, "Nordstjernen Navigator", (x + 2 * SS, 110 * SS), tag_font, (212, 222, 246), (166, 182, 226))


def canvas_texture(pixels: np.ndarray, rng) -> np.ndarray:
    height, width = pixels.shape[:2]
    cracks = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(cracks)
    for _ in range(140):
        x, y = rng.uniform(0, width), rng.uniform(0, height)
        angle = rng.uniform(0, 2 * math.pi)
        for _ in range(int(rng.integers(6, 26))):
            angle += rng.normal(0, 0.55)
            step = rng.uniform(4, 11) * SS
            nx, ny = x + math.cos(angle) * step, y + math.sin(angle) * step
            draw.line((x, y, nx, ny), fill=255, width=1)
            x, y = nx, ny
    crack = np.asarray(cracks, np.float32)[:, :, None] / 255.0
    return pixels * (1.0 - crack * 0.22)


def vignette(pixels: np.ndarray) -> np.ndarray:
    height, width = pixels.shape[:2]
    yy, xx = np.mgrid[0:height, 0:width].astype(np.float32)
    nx = (xx - width / 2) / (width / 2)
    ny = (yy - height / 2) / (height / 2)
    fall = np.clip(1.0 - 0.42 * (nx ** 2 + ny ** 2) ** 1.4, 0.35, 1.0)[:, :, None]
    warm = np.array((1.0, 0.92, 0.78), np.float32)[None, None, :]
    return pixels * lerp(warm, np.ones(3, np.float32)[None, None, :], fall) * fall


def gilded_frame(image: Image.Image, rng) -> None:
    draw = ImageDraw.Draw(image)
    width, height = image.size
    band = FRAME * SS
    for i in range(band):
        t = i / band
        shade = 0.35 + 0.65 * math.sin(t * math.pi) ** 1.6
        colour = tuple(int(c) for c in lerp(np.array((74, 50, 16), np.float32), np.array((224, 178, 72), np.float32), shade))
        draw.rectangle((i, i, width - 1 - i, height - 1 - i), outline=colour)
    for _ in range(320):
        side = rng.integers(4)
        along = rng.uniform(0, 1)
        depth = rng.uniform(2, band - 3)
        if side == 0:
            x, y = along * width, depth
        elif side == 1:
            x, y = along * width, height - 1 - depth
        elif side == 2:
            x, y = depth, along * height
        else:
            x, y = width - 1 - depth, along * height
        colour = jitter(lerp(np.array((120, 84, 30), np.float32), np.array((236, 200, 110), np.float32), rng.random() ** 2), rng, 8)
        dab(draw, x, y, rng.uniform(0, math.pi), rng.uniform(3, 9) * SS, rng.uniform(1, 2.2) * SS, colour)
    inner = band
    draw.rectangle((inner, inner, width - 1 - inner, height - 1 - inner), outline=(40, 26, 10))
    draw.rectangle((inner + 1, inner + 1, width - 2 - inner, height - 2 - inner), outline=(120, 92, 44))


def paint(version: str) -> Image.Image:
    rng = np.random.default_rng(SEED)
    width, height = WIDTH * SS, HEIGHT * SS
    base = underpainting(width, height, rng)
    image = brush_passes(base, rng).convert("RGBA")
    paint_aurora(image, rng)
    image = image.convert("RGB")
    paint_horizon(image, rng)
    pixels = np.asarray(image, np.float32) / 255.0
    stars = star_field(rng, width, height)
    pixels += stars * 0.8 + blur_rgb(stars, 2.5 * SS) * 0.35
    lights = city_lights(rng, width, height)
    pixels += lights * 0.85 + blur_rgb(lights, 3.0 * SS) * 0.55
    pixels += north_star(width, height)
    yy, xx = np.mgrid[0:height, 0:width].astype(np.float32)
    above = np.clip((limb_y(xx) - yy) / (26.0 * SS), 0, 1)
    rim = np.exp(-above * 3.2) * (above > 0)
    pixels += rim[:, :, None] * np.array((0.10, 0.30, 0.62), np.float32)[None, None, :]
    pixels = vignette(pixels)
    pixels = canvas_texture(pixels, rng)
    image = Image.fromarray(np.clip(pixels * 255.0, 0, 255).astype(np.uint8), "RGB")
    paint_title(image, version)
    gilded_frame(image, rng)
    return image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)


def main() -> None:
    version = project_version()
    output = ROOT / "data" / f"about-splash-{version}.png"
    paint(version).save(output, optimize=True)
    print(f"wrote {output}")


if __name__ == "__main__":
    main()
