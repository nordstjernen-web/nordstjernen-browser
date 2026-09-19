# build-splash-art.py — draws the about:start splash as an xkcd-style comic of Noah's ark, one frame at a time.
from __future__ import annotations

import math
from pathlib import Path
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parent.parent
WIDTH = 940
HEIGHT = 320
SS = 3
SEED = 20260919
TAU = 2.0 * math.pi

OUT_SCALE = 1.5
PAPER = (255, 255, 255)
INK = (16, 16, 16)
WATER = INK
SUN = PAPER
PINK = PAPER
LINE = 1.5
HORIZON = 148.0
MONO = True

def project_version() -> str:
    text = (ROOT / "meson.build").read_text(encoding="utf-8")
    match = re.search(r"^\s*version:\s*'([^']+)'", text, re.MULTILINE)
    if match is None:
        raise RuntimeError("could not read version from meson.build")
    return match.group(1).split("-", 1)[0]


class Pen:
    def __init__(self, image: Image.Image, seed: int, gait: float = 0.0):
        self.image = image
        self.draw = ImageDraw.Draw(image)
        self.rng = np.random.default_rng(seed)
        self.gait = gait

    def wobble(self, points, amp: float = 1.0, step: float = 5.0, closed: bool = False):
        pts = [(float(x) * SS, float(y) * SS) for x, y in points]
        if closed:
            pts.append(pts[0])
        dense = []
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            length = math.hypot(x1 - x0, y1 - y0)
            n = max(1, int(length / (step * SS)))
            for i in range(n):
                t = i / n
                dense.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))
        dense.append(pts[-1])
        if len(dense) < 3:
            return dense
        noise = self.rng.normal(0.0, 1.0, len(dense)).astype(np.float32)
        kernel = np.array((0.15, 0.2, 0.3, 0.2, 0.15), np.float32)
        smooth = np.convolve(noise, kernel, mode="same") * amp * SS
        out = []
        for i, (x, y) in enumerate(dense):
            px, py = dense[max(0, i - 1)]
            nx, ny = dense[min(len(dense) - 1, i + 1)]
            tx, ty = nx - px, ny - py
            norm = math.hypot(tx, ty) or 1.0
            ox, oy = -ty / norm, tx / norm
            out.append((x + ox * smooth[i], y + oy * smooth[i]))
        if closed:
            out[-1] = out[0]
        return out

    def stroke(self, points, width: float = LINE, amp: float = 0.7, closed: bool = False, fill=INK):
        if MONO and fill != PAPER:
            fill = INK
        pts = self.wobble(points, amp=amp, closed=closed)
        w = max(1, round(width * SS))
        self.draw.line(pts, fill=fill, width=w, joint="curve")
        r = w / 2.0
        for x, y in (pts[0], pts[-1]):
            self.draw.ellipse((x - r, y - r, x + r, y + r), fill=fill)

    def ellipse(self, cx: float, cy: float, rx: float, ry: float, width: float = LINE,
                amp: float = 0.8, fill=None, start: float = 0.0, end: float = 360.0,
                rot: float = 0.0, outline=INK):
        pts = []
        n = max(12, int((rx + ry) * 0.9))
        for i in range(n + 1):
            a = math.radians(start + (end - start) * i / n)
            x, y = rx * math.cos(a), ry * math.sin(a)
            c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
            pts.append((cx + x * c - y * s, cy + x * s + y * c))
        closed = end - start >= 360.0
        wob = self.wobble(pts, amp=amp * 0.7, closed=closed)
        if MONO and fill is not None and fill != INK:
            fill = PAPER
        if fill is not None:
            self.draw.polygon(wob, fill=fill)
        if outline is not None:
            self.draw.line(wob, fill=outline, width=max(1, round(width * SS)), joint="curve")

    def shape(self, points, fill, width: float = LINE, amp: float = 0.7, outline=INK):
        if MONO and fill != INK:
            fill = PAPER
        wob = self.wobble(points, amp=amp, closed=True)
        self.draw.polygon(wob, fill=fill)
        if outline is not None:
            self.draw.line(wob, fill=outline, width=max(1, round(width * SS)), joint="curve")

    def dot(self, x: float, y: float, r: float = 1.5, fill=INK):
        if MONO and fill != PAPER:
            fill = INK
        self.draw.ellipse(((x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS), fill=fill)

    def scribble(self, cx: float, cy: float, rx: float, ry: float, turns: int = 4,
                 width: float = 1.3, fill=None):
        if fill is not None:
            self.ellipse(cx, cy, rx, ry, fill=fill, outline=None)
        pts = []
        n = turns * 14
        for i in range(n + 1):
            a = i / n * turns * TAU
            s = 0.35 + 0.65 * (i / n)
            pts.append((cx + rx * s * math.cos(a), cy + ry * s * math.sin(a)))
        self.stroke(pts, width=width, amp=0.6)

    def legs(self, x0: float, x1: float, top: float, ground: float, count: int = 4,
             w: float = LINE, swing: float = 2.0):
        for i in range(count):
            t = i / max(1, count - 1)
            lx = x0 + (x1 - x0) * t
            phase = self.gait + (0.5 if i % 2 else 0.0)
            dx = swing * math.sin(TAU * phase)
            self.stroke([(lx, top), (lx + dx, ground)], width=w, amp=0.4)


GLYPHS = {
    "A": (9, [[(0, 0), (4.5, 14), (9, 0)], [(2, 5), (7, 5)]]),
    "B": (9, [[(0, 0), (0, 14), (6, 14), (8, 12.5), (8, 9.5), (6, 8), (0, 8)],
              [(6, 8), (8.5, 6), (8.5, 2), (6, 0), (0, 0)]]),
    "C": (9, [[(8.5, 11), (7, 13.3), (4.5, 14), (2, 13), (0.5, 10.5), (0.5, 3.5), (2, 1),
               (4.5, 0), (7, 0.7), (8.5, 3)]]),
    "D": (9, [[(0, 0), (0, 14), (4.5, 14), (7.5, 12.5), (9, 9), (9, 5), (7.5, 1.5), (4.5, 0), (0, 0)]]),
    "E": (8, [[(8, 14), (0, 14), (0, 0), (8, 0)], [(0, 7), (6, 7)]]),
    "F": (8, [[(8, 14), (0, 14), (0, 0)], [(0, 7), (6, 7)]]),
    "G": (9, [[(8.5, 11), (7, 13.3), (4.5, 14), (2, 13), (0.5, 10.5), (0.5, 3.5), (2, 1),
               (4.5, 0), (7, 0.7), (8.5, 3), (8.5, 6), (5, 6)]]),
    "H": (9, [[(0, 0), (0, 14)], [(9, 0), (9, 14)], [(0, 7), (9, 7)]]),
    "I": (4, [[(0, 14), (4, 14)], [(2, 14), (2, 0)], [(0, 0), (4, 0)]]),
    "J": (7, [[(7, 14), (7, 3), (5.5, 0.5), (3, 0), (1, 1), (0, 3.5)]]),
    "K": (9, [[(0, 0), (0, 14)], [(8, 14), (0, 5)], [(3, 8), (8.5, 0)]]),
    "L": (8, [[(0, 14), (0, 0), (8, 0)]]),
    "M": (11, [[(0, 0), (0, 14), (5.5, 3), (11, 14), (11, 0)]]),
    "N": (9, [[(0, 0), (0, 14), (9, 0), (9, 14)]]),
    "O": (9, [[(4.5, 14), (1.8, 12.8), (0.4, 10), (0.4, 4), (1.8, 1.2), (4.5, 0), (7.2, 1.2),
               (8.6, 4), (8.6, 10), (7.2, 12.8), (4.5, 14)]]),
    "P": (9, [[(0, 0), (0, 14), (6, 14), (8.5, 12.5), (8.5, 9), (6, 7.5), (0, 7.5)]]),
    "Q": (9, [[(4.5, 14), (1.8, 12.8), (0.4, 10), (0.4, 4), (1.8, 1.2), (4.5, 0), (7.2, 1.2),
               (8.6, 4), (8.6, 10), (7.2, 12.8), (4.5, 14)], [(6, 3), (9.5, -1)]]),
    "R": (9, [[(0, 0), (0, 14), (6, 14), (8.5, 12.5), (8.5, 9), (6, 7.5), (0, 7.5)], [(4.5, 7.5), (9, 0)]]),
    "S": (9, [[(8.5, 11.5), (7, 13.5), (4.5, 14), (2, 13), (0.8, 11), (1.2, 9), (3, 7.5), (6, 6.5),
               (8, 5), (8.6, 3), (7.5, 1), (5, 0), (2, 0.4), (0.4, 2.5)]]),
    "T": (9, [[(0, 14), (9, 14)], [(4.5, 14), (4.5, 0)]]),
    "U": (9, [[(0, 14), (0, 4), (1.2, 1.2), (4.5, 0), (7.8, 1.2), (9, 4), (9, 14)]]),
    "V": (9, [[(0, 14), (4.5, 0), (9, 14)]]),
    "W": (12, [[(0, 14), (3, 0), (6, 10), (9, 0), (12, 14)]]),
    "X": (9, [[(0, 14), (9, 0)], [(0, 0), (9, 14)]]),
    "Y": (9, [[(0, 14), (4.5, 7), (9, 14)], [(4.5, 7), (4.5, 0)]]),
    "Z": (9, [[(0, 14), (9, 14), (0, 0), (9, 0)]]),
    "0": (9, [[(4.5, 14), (2, 13), (0.6, 10), (0.6, 4), (2, 1), (4.5, 0), (7, 1), (8.4, 4),
               (8.4, 10), (7, 13), (4.5, 14)]]),
    "1": (5, [[(0, 11), (2.5, 14), (2.5, 0)]]),
    "2": (9, [[(0.8, 11), (2, 13.2), (4.5, 14), (7, 13), (8, 11), (7.5, 8.5), (0.5, 0), (8.5, 0)]]),
    "3": (9, [[(0.8, 12.5), (3, 14), (6, 14), (8, 12), (8, 9.5), (6, 7.5), (3.5, 7.5)],
              [(6, 7.5), (8.5, 5.5), (8.5, 2.5), (6.5, 0), (3, 0), (0.5, 1.5)]]),
    "4": (9, [[(7, 0), (7, 14), (0, 4), (9, 4)]]),
    "5": (9, [[(8, 14), (1.5, 14), (0.8, 7.5), (3.5, 8.5), (6, 8.5), (8.5, 6.5), (8.5, 2.5),
               (6.5, 0), (3, 0), (0.5, 1.5)]]),
    "6": (9, [[(7.8, 12), (5.5, 14), (3, 13.5), (1, 11), (0.5, 7), (0.8, 3), (2.5, 0.5), (5, 0),
               (7.5, 1), (8.5, 3.5), (8, 6), (6, 7.5), (3.5, 7.5), (1.2, 6.2)]]),
    "7": (9, [[(0.5, 14), (9, 14), (3.5, 0)]]),
    "8": (9, [[(4.5, 7.5), (2, 8.5), (1, 10.5), (1.5, 13), (4.5, 14), (7.5, 13), (8, 10.5),
               (7, 8.5), (4.5, 7.5), (1.5, 6), (0.8, 3), (2, 0.7), (4.5, 0), (7, 0.7), (8.2, 3),
               (7.5, 6), (4.5, 7.5)]]),
    "9": (9, [[(1.2, 2), (3.5, 0), (6, 0.5), (8, 3), (8.5, 7), (8.2, 11), (6.5, 13.5), (4, 14),
               (1.5, 13), (0.5, 10.5), (1, 8), (3, 6.5), (5.5, 6.5), (7.8, 7.8)]]),
    ".": (2, [[(1, 0.6), (1.2, 0.3)]]),
    ",": (2, [[(1.4, 1.2), (0.6, -1.8)]]),
    "'": (2, [[(1.2, 14), (0.8, 11)]]),
    "?": (9, [[(0.8, 11.5), (2, 13.5), (4.5, 14), (7, 13), (8, 11), (7, 8.5), (4.5, 7), (4.5, 4)],
              [(4.5, 0.8), (4.6, 0.4)]]),
    "!": (2, [[(1, 14), (1, 4)], [(1, 0.8), (1.1, 0.4)]]),
    "-": (7, [[(0.5, 6.5), (6.5, 6.5)]]),
    ":": (2, [[(1, 9), (1.1, 8.6)], [(1, 1), (1.1, 0.6)]]),
}


def letter(pen: Pen, text: str, x: float, y: float, size: float, spacing: float = 0.0,
           fill=INK, width: float = 0.0) -> float:
    rng = np.random.default_rng((hash(text) & 0xFFFF) + 11)
    unit = size / 14.0
    cursor = x
    stroke_w = width or max(1.0, size * 0.105)
    for ch in text.upper():
        if ch == " ":
            cursor += 6.5 * unit
            continue
        if ch not in GLYPHS:
            cursor += 6 * unit
            continue
        advance, strokes = GLYPHS[ch]
        tilt = rng.uniform(-0.06, 0.06)
        lift = rng.uniform(-0.35, 0.35) * unit
        scale = rng.uniform(0.96, 1.05)
        for stroke in strokes:
            pts = [(cursor + (gx + gy * tilt) * unit * scale, y - gy * unit * scale + lift) for gx, gy in stroke]
            if len(pts) == 2 and math.hypot(pts[1][0] - pts[0][0], pts[1][1] - pts[0][1]) < 0.6 * unit:
                pen.dot(pts[0][0], pts[0][1], stroke_w * 0.55, fill=fill)
                continue
            pen.stroke(pts, width=stroke_w, amp=0.28 * unit ** 0.5, fill=fill)
        cursor += (advance + 3.2 + spacing) * unit
    return cursor


def text_width(text: str, size: float, spacing: float = 0.0) -> float:
    unit = size / 14.0
    total = 0.0
    for ch in text.upper():
        if ch == " ":
            total += 6.5 * unit
        elif ch in GLYPHS:
            total += (GLYPHS[ch][0] + 3.2 + spacing) * unit
        else:
            total += 6 * unit
    return total


def title(pen: Pen, version: str) -> None:
    letter(pen, "Yet another web browser", 34, 58, 30, spacing=0.6)
    end = letter(pen, "Nordstjernen", 36, 88, 12, spacing=0.4)
    letter(pen, version, end + 9, 88, 12, spacing=0.4)


def panel(pen: Pen) -> None:
    pen.stroke([(6, 6), (WIDTH - 6, 6), (WIDTH - 6, HEIGHT - 6), (6, HEIGHT - 6)],
               width=1.6, amp=0.8, closed=True)


def sun(pen: Pen, cx: float, cy: float, r: float, t: float) -> None:
    pen.ellipse(cx, cy, r, r, fill=SUN, amp=0.9)
    for k in range(12):
        a = TAU * k / 12 + math.sin(TAU * t) * 0.03
        pulse = 1.0 + 0.18 * math.sin(TAU * t * 2 + k * 1.3)
        inner = r + 5
        outer = r + (14 if k % 2 else 22) * pulse
        pen.stroke([(cx + inner * math.cos(a), cy + inner * math.sin(a)),
                    (cx + outer * math.cos(a), cy + outer * math.sin(a))], width=1.4, amp=0.4)


def cloud(pen: Pen, cx: float, cy: float, w: float, h: float, bumps: int = 5, dark: bool = False) -> None:
    left, right = cx - w / 2, cx + w / 2
    base = cy + h * 0.45
    pts = [(left, base), (right, base)]
    for i in range(bumps - 1, -1, -1):
        x0 = left + w * i / bumps
        x1 = left + w * (i + 1) / bumps
        r = (x1 - x0) / 2
        lift = h * (0.75 + 0.45 * math.sin(math.pi * (i + 0.5) / bumps))
        for k in range(9):
            a = math.pi * k / 8
            pts.append(((x0 + x1) / 2 + r * math.cos(a), base - lift * math.sin(a)))
    pen.shape(pts, fill=(224, 224, 228) if dark else PAPER, amp=0.8)
    if dark:
        for k in range(int(w / 9)):
            x = left + 8 + k * 9
            pen.stroke([(x, base - 3), (x - 4, base - 12 - (k % 3) * 3)], width=1.0, amp=0.3)


def rain(pen: Pen, cx: float, cy: float, w: float, top: float, length: float, t: float) -> None:
    rng = np.random.default_rng(77)
    for k in range(int(w / 6)):
        x = cx - w / 2 + rng.uniform(2, 6) + k * 6
        speed = rng.uniform(0.9, 1.2)
        phase = (t * speed + rng.uniform(0, 1)) % 1.0
        y = top + phase * length
        pen.stroke([(x, y), (x - 2.5, y + 9)], width=1.1, amp=0.2)


def mountains(pen: Pen) -> None:
    pts = [(560, HORIZON), (610, HORIZON - 22), (650, HORIZON - 8), (700, HORIZON - 30),
           (760, HORIZON - 12), (800, HORIZON - 26), (860, HORIZON - 4), (940, HORIZON - 14),
           (940, HORIZON), (560, HORIZON)]
    pen.shape(pts, fill=PAPER, width=1.4, amp=0.9)
    pen.stroke([(690, HORIZON - 26), (700, HORIZON - 30), (712, HORIZON - 24)], width=1.0, amp=0.3)


def shore(pen: Pen) -> None:
    pts = [(0, HORIZON), (580, HORIZON), (556, HORIZON + 14), (540, HORIZON + 34),
           (546, HORIZON + 56), (500, HORIZON + 68), (470, HORIZON + 90), (462, HORIZON + 120),
           (490, HORIZON + 150), (540, HORIZON + 172), (0, HORIZON + 172)]
    pen.shape(pts, fill=PAPER, width=1.6, amp=1.2, outline=None)
    pen.stroke(pts[1:10], width=1.6, amp=1.2)
    pen.stroke([(8, HORIZON), (932, HORIZON)], width=1.3, amp=0.9)
    for k in range(9):
        x = 60 + k * 44
        y = HORIZON + 30 + k * 3
        pen.stroke([(x, y), (x + 5, y - 2), (x + 10, y)], width=1.0, amp=0.3)


def waves(pen: Pen, t: float) -> None:
    rng = np.random.default_rng(31)
    rows = [(HORIZON + 10, 30, 0.7), (HORIZON + 24, 34, 0.9), (HORIZON + 42, 40, 1.1),
            (HORIZON + 64, 48, 1.4), (HORIZON + 92, 56, 1.7), (HORIZON + 126, 66, 2.1),
            (HORIZON + 160, 76, 2.5)]
    for row, (y, gap, amp) in enumerate(rows):
        drift = (t * gap * (1 if row % 2 else -1))
        x = 396 + rng.uniform(0, gap) + row * 17 + drift
        x = x % gap + 380
        while x < 905:
            phase = TAU * (t * 1.2 + row * 0.37)
            pts = [(x + k * gap * 0.06, y + amp * math.sin(k * 0.95 + phase)) for k in range(8)]
            pen.stroke(pts, width=0.9 + row * 0.1, amp=0.15)
            x += gap
        y += 0


def ark(pen: Pen, x: float, y: float, s: float, roll: float, t: float = 0.0) -> None:
    layer = Image.new("RGBA", pen.image.size, (0, 0, 0, 0))
    lp = Pen(layer, 4242)
    hull = [(x - 118 * s, y - 26 * s), (x - 128 * s, y - 4 * s), (x - 104 * s, y + 24 * s),
            (x + 104 * s, y + 24 * s), (x + 138 * s, y - 4 * s), (x + 124 * s, y - 26 * s)]
    lp.shape(hull, fill=PAPER, width=LINE * 1.3, amp=1.1)
    for k in range(2):
        yy = y - 10 * s + k * 14 * s
        lp.stroke([(x - 116 * s + k * 6 * s, yy), (x + 122 * s - k * 6 * s, yy)], width=1.1, amp=0.7)
    for k in range(7):
        xx = x - 90 * s + k * 32 * s
        lp.stroke([(xx, y - 22 * s), (xx + 2 * s, y + 20 * s)], width=0.9, amp=0.4)
    deck = y - 28 * s
    lp.stroke([(x - 118 * s, deck + 2 * s), (x + 124 * s, deck + 2 * s)], width=LINE * 1.2, amp=0.9)
    house = [(x - 84 * s, deck), (x - 84 * s, deck - 44 * s), (x + 92 * s, deck - 44 * s), (x + 92 * s, deck)]
    lp.shape(house, fill=PAPER, width=LINE * 1.2, amp=0.9)
    roof = [(x - 98 * s, deck - 42 * s), (x + 4 * s, deck - 78 * s), (x + 106 * s, deck - 42 * s)]
    lp.shape(roof + [(x + 4 * s, deck - 42 * s)], fill=PAPER, width=LINE * 1.3, amp=0.9, outline=None)
    lp.stroke(roof, width=LINE * 1.3, amp=0.9)
    for k in range(4):
        wx = x - 66 * s + k * 42 * s
        lp.stroke([(wx, deck - 12 * s), (wx, deck - 32 * s), (wx + 18 * s, deck - 32 * s),
                   (wx + 18 * s, deck - 12 * s)], width=1.1, closed=True, amp=0.5)
    lp.stroke([(x + 4 * s, deck - 78 * s), (x + 4 * s, deck - 100 * s)], width=1.2)
    flutter = math.sin(TAU * t * 2)
    lp.shape([(x + 4 * s, deck - 100 * s), (x + 24 * s, deck - 94 * s - 3 * s * flutter),
              (x + 4 * s, deck - 88 * s)], fill=PAPER, width=1.1, amp=0.6)
    stick_figure(lp, x - 100 * s, deck + 1 * s, h=30 * s, beard=True, staff=True, wave=True,
                 facing=-1, wave_phase=t * 2)
    giraffe_head(lp, x + 60 * s, deck - 44 * s, s * 0.9)
    giraffe_head(lp, x + 82 * s, deck - 44 * s, s * 0.7)
    for k, (ox, oy) in enumerate(((-50, -22), (-8, -22), (34, -22))):
        peek(lp, x + ox * s + 9 * s, deck + oy * s, s, k)
    rotated = layer.rotate(roll, resample=Image.Resampling.BICUBIC, center=(x * SS, y * SS))
    pen.image.paste(rotated, (0, 0), rotated)


def peek(pen: Pen, x: float, y: float, s: float, kind: int) -> None:
    if kind == 0:
        pen.ellipse(x, y, 6 * s, 5 * s, fill=PAPER)
        pen.stroke([(x - 4 * s, y - 4 * s), (x - 3 * s, y - 9 * s), (x, y - 5 * s)], width=1.0, amp=0.3)
        pen.stroke([(x + 4 * s, y - 4 * s), (x + 3 * s, y - 9 * s), (x, y - 5 * s)], width=1.0, amp=0.3)
        pen.dot(x - 2 * s, y - 1 * s, 0.7 * s)
        pen.dot(x + 2 * s, y - 1 * s, 0.7 * s)
    elif kind == 1:
        pen.ellipse(x, y, 7 * s, 5 * s, fill=(120, 120, 128))
        pen.stroke([(x + 6 * s, y + 1 * s), (x + 10 * s, y + 5 * s), (x + 8 * s, y + 10 * s)], width=1.2, amp=0.5)
        pen.ellipse(x - 4 * s, y, 4 * s, 4.5 * s, fill=(120, 120, 128))
        pen.dot(x + 2 * s, y - 1.5 * s, 0.7 * s)
    else:
        pen.scribble(x, y, 7 * s, 6 * s, turns=4, width=1.0, fill=(220, 170, 90))
        pen.ellipse(x, y, 4 * s, 3.5 * s, fill=(230, 190, 120))
        pen.dot(x - 1.5 * s, y - 0.8 * s, 0.6 * s)
        pen.dot(x + 1.5 * s, y - 0.8 * s, 0.6 * s)


def giraffe_head(pen: Pen, x: float, y: float, s: float) -> None:
    pen.stroke([(x, y), (x + 4 * s, y - 34 * s)], width=LINE * 1.3)
    hx, hy = x + 6 * s, y - 38 * s
    pen.ellipse(hx, hy, 6 * s, 3.6 * s, fill=(240, 210, 120), rot=-15)
    pen.stroke([(hx - 1 * s, hy - 3 * s), (hx - 2 * s, hy - 8 * s)], width=1.2)
    pen.stroke([(hx + 2 * s, hy - 3 * s), (hx + 2 * s, hy - 8 * s)], width=1.2)
    pen.dot(hx + 3 * s, hy - 1 * s, 1.0 * s)
    for k in range(4):
        pen.ellipse(x + 1 * s + k * 0.9 * s, y - 6 * s - k * 8 * s, 1.6 * s, 1.4 * s, width=0.9, fill=(150, 100, 40))


def stick_figure(pen: Pen, x: float, y: float, h: float = 34.0, beard: bool = False,
                 staff: bool = False, wave: bool = False, facing: int = 1,
                 wave_phase: float = 0.0, clipboard: bool = False) -> None:
    head_r = h * 0.14
    pen.ellipse(x, y - h + head_r, head_r, head_r, fill=PAPER)
    pen.stroke([(x, y - h + head_r * 2), (x, y - h * 0.42)])
    pen.stroke([(x, y - h * 0.42), (x - h * 0.16, y)])
    pen.stroke([(x, y - h * 0.42), (x + h * 0.16, y)])
    shoulder = y - h * 0.72
    if wave:
        swing = math.sin(TAU * wave_phase) * h * 0.1
        pen.stroke([(x, shoulder), (x + facing * h * 0.22, shoulder - h * 0.12),
                    (x + facing * h * 0.3 + swing, shoulder - h * 0.36)])
    elif clipboard:
        hx, hy = x + facing * h * 0.26, y - h * 0.5
        pen.stroke([(x, shoulder), (hx, hy)])
        pen.shape([(hx - 3, hy - 9), (hx + 4, hy - 9), (hx + 4, hy + 2), (hx - 3, hy + 2)],
                  fill=PAPER, width=1.0, amp=0.3)
        pen.stroke([(hx - 1.5, hy - 6), (hx + 2.5, hy - 6)], width=0.8, amp=0.1)
        pen.stroke([(hx - 1.5, hy - 3), (hx + 2.5, hy - 3)], width=0.8, amp=0.1)
        pen.stroke([(hx - 1.5, hy), (hx + 1, hy)], width=0.8, amp=0.1)
    else:
        pen.stroke([(x, shoulder), (x + facing * h * 0.24, y - h * 0.4)])
    if clipboard:
        pen.stroke([(x, shoulder), (x + facing * h * 0.2, y - h * 0.44), (x + facing * h * 0.24, y - h * 0.5)])
    else:
        pen.stroke([(x, shoulder), (x - facing * h * 0.24, y - h * 0.44)])
    if staff:
        sx = x - facing * h * 0.26
        pen.stroke([(sx, y - h * 0.95), (sx, y + 2)])
        pen.ellipse(sx, y - h * 0.98, 2.0, 2.0)
    if beard:
        cy = y - h + head_r * 1.5
        pen.stroke([(x - head_r * 0.9, cy), (x - head_r * 0.6, cy + head_r * 1.6),
                    (x, cy + head_r * 2.3), (x + head_r * 0.6, cy + head_r * 1.6),
                    (x + head_r * 0.9, cy)], amp=1.3, width=1.5)


def giraffe(pen: Pen, x: float, g: float, s: float) -> None:
    body_w, body_h = 26 * s, 13 * s
    by = g - 22 * s
    pen.ellipse(x, by, body_w / 2, body_h / 2, fill=(240, 210, 120))
    pen.legs(x - body_w * 0.35, x + body_w * 0.35, by + body_h * 0.3, g, swing=2.5 * s)
    giraffe_head(pen, x + body_w * 0.42, by - body_h * 0.2, s)
    for k in range(5):
        px = x + (k - 2) * body_w * 0.16
        py = by + (k % 2 - 0.5) * body_h * 0.35
        pen.ellipse(px, py, 2.2 * s, 1.8 * s, width=1.0, fill=(150, 100, 40))
    pen.stroke([(x - body_w * 0.5, by), (x - body_w * 0.6, by + 10 * s)], width=1.3)


def elephant(pen: Pen, x: float, g: float, s: float) -> None:
    grey = (150, 150, 158)
    body_w, body_h = 40 * s, 24 * s
    by = g - 20 * s
    pen.ellipse(x, by, body_w / 2, body_h / 2, fill=grey)
    pen.legs(x - body_w * 0.33, x + body_w * 0.33, by + body_h * 0.25, g, w=LINE * 1.6, swing=2.0 * s)
    hx, hy = x + body_w * 0.55, by - body_h * 0.28
    pen.ellipse(hx, hy, 9 * s, 8 * s, fill=grey)
    pen.ellipse(hx - 4 * s, hy, 6 * s, 7 * s, fill=(170, 170, 178))
    pen.stroke([(hx + 7 * s, hy + 2 * s), (hx + 12 * s, hy + 10 * s), (hx + 9 * s, hy + 20 * s),
                (hx + 13 * s, hy + 24 * s)], width=LINE * 1.3, amp=0.8)
    pen.stroke([(hx + 6 * s, hy + 6 * s), (hx + 11 * s, hy + 9 * s)], width=1.3, fill=(240, 240, 230))
    pen.dot(hx + 3 * s, hy - 2 * s, 1.2 * s)
    pen.stroke([(x - body_w * 0.5, by - 2 * s), (x - body_w * 0.62, by + 8 * s)], width=1.3)


def lion(pen: Pen, x: float, g: float, s: float) -> None:
    tan = (226, 186, 110)
    body_w, body_h = 30 * s, 14 * s
    by = g - 14 * s
    pen.ellipse(x, by, body_w / 2, body_h / 2, fill=tan)
    pen.legs(x - body_w * 0.35, x + body_w * 0.35, by + body_h * 0.3, g, swing=2.5 * s)
    hx, hy = x + body_w * 0.5, by - body_h * 0.4
    pen.scribble(hx, hy, 11 * s, 11 * s, turns=5, width=1.2, fill=(190, 130, 60))
    pen.ellipse(hx, hy, 6 * s, 5.5 * s, fill=tan)
    pen.dot(hx - 2 * s, hy - 1 * s, 1.0 * s)
    pen.dot(hx + 2 * s, hy - 1 * s, 1.0 * s)
    pen.stroke([(hx - 1.5 * s, hy + 2 * s), (hx + 1.5 * s, hy + 2 * s)], width=1.1)
    pen.stroke([(x - body_w * 0.5, by - 3 * s), (x - body_w * 0.7, by - 10 * s),
                (x - body_w * 0.8, by - 4 * s)], width=1.3, amp=0.8)
    pen.scribble(x - body_w * 0.8, by - 3 * s, 2.5 * s, 2.5 * s, turns=2, width=1.0)


def zebra(pen: Pen, x: float, g: float, s: float) -> None:
    body_w, body_h = 30 * s, 14 * s
    by = g - 16 * s
    pen.ellipse(x, by, body_w / 2, body_h / 2, fill=PAPER)
    pen.legs(x - body_w * 0.35, x + body_w * 0.35, by + body_h * 0.3, g, swing=2.5 * s)
    nx = x + body_w * 0.42
    pen.stroke([(nx, by - body_h * 0.3), (nx + 6 * s, by - 14 * s)], width=LINE * 1.4)
    hx, hy = nx + 9 * s, by - 16 * s
    pen.ellipse(hx, hy, 5.5 * s, 3.2 * s, fill=PAPER, rot=-20)
    pen.dot(hx + 2 * s, hy - 0.5 * s, 1.0 * s)
    for k in range(6):
        px = x - body_w * 0.4 + k * body_w * 0.15
        pen.stroke([(px, by - body_h * 0.45), (px + 2 * s, by + body_h * 0.45)], width=1.6 * s, amp=0.4)
    pen.stroke([(x - body_w * 0.5, by - 2 * s), (x - body_w * 0.62, by + 8 * s)], width=1.3)


def penguin(pen: Pen, x: float, g: float, s: float) -> None:
    by = g - 11 * s
    pen.ellipse(x, by, 7 * s, 11 * s, fill=INK)
    pen.ellipse(x + 0.5 * s, by + 2 * s, 4 * s, 7 * s, width=1.0, fill=PAPER)
    pen.ellipse(x, by - 9 * s, 4 * s, 4 * s, fill=INK)
    pen.shape([(x + 3 * s, by - 10 * s), (x + 8 * s, by - 8.5 * s), (x + 3 * s, by - 7 * s)], fill=(230, 150, 40), width=1.0)
    pen.stroke([(x - 6 * s, by - 3 * s), (x - 10 * s + 2 * s * math.sin(TAU * pen.gait), by + 4 * s)], width=1.6)
    pen.stroke([(x - 3 * s, g), (x + 1 * s, g)], width=1.4, fill=(230, 150, 40))
    pen.stroke([(x + 1 * s, g), (x + 5 * s, g)], width=1.4, fill=(230, 150, 40))


def snake(pen: Pen, x: float, g: float, s: float) -> None:
    pts = [(x + i * 4 * s, g - 3 * s + 3 * s * math.sin(i * 1.1 + TAU * pen.gait)) for i in range(9)]
    pen.stroke(pts, width=LINE * 1.6 * s, amp=0.4, fill=(96, 150, 70))
    pen.stroke(pts, width=LINE * 0.7, amp=0.4)
    hx, hy = pts[-1]
    pen.ellipse(hx + 2 * s, hy - 1 * s, 3.5 * s, 2.2 * s, fill=(96, 150, 70))
    pen.stroke([(hx + 5 * s, hy - 1 * s), (hx + 9 * s, hy - 2 * s)], width=1.0, fill=(180, 30, 30))
    pen.dot(hx + 3 * s, hy - 1.6 * s, 0.8 * s)


def rabbit(pen: Pen, x: float, g: float, s: float) -> None:
    fur = (210, 200, 190)
    by = g - 7 * s
    pen.ellipse(x, by, 8 * s, 6.5 * s, fill=fur)
    pen.ellipse(x + 8 * s, by - 5 * s, 4 * s, 4 * s, fill=fur)
    pen.ellipse(x + 8 * s, by - 13 * s, 1.6 * s, 5 * s, fill=fur, rot=-12)
    pen.ellipse(x + 11 * s, by - 12.5 * s, 1.6 * s, 5 * s, fill=fur, rot=8)
    pen.dot(x + 10 * s, by - 6 * s, 0.9 * s)
    pen.scribble(x - 8 * s, by - 1 * s, 2 * s, 2 * s, turns=2, width=1.0, fill=PAPER)
    pen.stroke([(x + 3 * s, by + 5 * s), (x + 5 * s + 2 * s * math.sin(TAU * pen.gait), g)], width=1.2)


def turtle(pen: Pen, x: float, g: float, s: float) -> None:
    shell = (120, 160, 90)
    pen.ellipse(x, g - 4 * s, 11 * s, 7 * s, start=180, end=360, fill=shell)
    pen.stroke([(x - 11 * s, g - 4 * s), (x + 11 * s, g - 4 * s)], width=LINE)
    pen.ellipse(x + 13 * s, g - 5 * s, 3.5 * s, 2.6 * s, fill=(150, 180, 110))
    pen.dot(x + 14 * s, g - 5.6 * s, 0.7 * s)
    for k in (-7, -2, 3):
        pen.stroke([(x + k * s, g - 10 * s), (x + k * s + 4 * s, g - 10 * s), (x + k * s + 2 * s, g - 5 * s)], width=0.9, amp=0.3)
    pen.stroke([(x - 7 * s, g - 4 * s), (x - 9 * s, g)], width=1.3)
    pen.stroke([(x + 7 * s, g - 4 * s), (x + 9 * s, g)], width=1.3)


def monkey(pen: Pen, x: float, g: float, s: float) -> None:
    fur = (150, 110, 70)
    by = g - 12 * s
    pen.ellipse(x, by, 6 * s, 8 * s, fill=fur)
    pen.ellipse(x, by - 11 * s, 5 * s, 4.5 * s, fill=fur)
    pen.ellipse(x, by - 10 * s, 3.2 * s, 2.6 * s, width=0.9, fill=(220, 190, 150))
    pen.ellipse(x - 5 * s, by - 11 * s, 1.6 * s, 1.6 * s, width=1.1, fill=fur)
    pen.ellipse(x + 5 * s, by - 11 * s, 1.6 * s, 1.6 * s, width=1.1, fill=fur)
    pen.dot(x - 1.4 * s, by - 11.5 * s, 0.6 * s)
    pen.dot(x + 1.4 * s, by - 11.5 * s, 0.6 * s)
    pen.stroke([(x - 5 * s, by - 4 * s), (x - 11 * s, by + 3 * s)], width=1.3)
    pen.stroke([(x + 5 * s, by - 4 * s), (x + 11 * s, by - 8 * s - 3 * s * math.sin(TAU * pen.gait))], width=1.3)
    pen.stroke([(x - 2 * s, by + 7 * s), (x - 3 * s, g)], width=1.3)
    pen.stroke([(x + 2 * s, by + 7 * s), (x + 3 * s, g)], width=1.3)
    tail = [(x + 5 * s + 6 * s * math.sin(i * 0.9), by + 5 * s - i * 2.2 * s) for i in range(8)]
    pen.stroke(tail, width=1.2, amp=0.5)


def kangaroo(pen: Pen, x: float, g: float, s: float) -> None:
    fur = (190, 140, 90)
    by = g - 14 * s
    pen.ellipse(x, by, 9 * s, 12 * s, fill=fur, rot=-15)
    pen.stroke([(x + 4 * s, by - 10 * s), (x + 8 * s, by - 24 * s)], width=LINE * 1.5 * s, fill=fur)
    pen.stroke([(x + 4 * s, by - 10 * s), (x + 8 * s, by - 24 * s)], width=LINE * 0.6)
    pen.ellipse(x + 10 * s, by - 26 * s, 5 * s, 3 * s, fill=fur, rot=-20)
    pen.stroke([(x + 8 * s, by - 28 * s), (x + 7 * s, by - 34 * s)], width=1.1)
    pen.stroke([(x + 11 * s, by - 28 * s), (x + 11 * s, by - 34 * s)], width=1.1)
    pen.dot(x + 12 * s, by - 27 * s, 0.8 * s)
    pen.stroke([(x + 7 * s, by - 6 * s), (x + 12 * s, by - 2 * s)], width=1.2)
    hop = 2 * s * math.sin(TAU * pen.gait)
    pen.stroke([(x, by + 10 * s), (x - 3 * s, g), (x + 10 * s + hop, g)], width=LINE * 1.3, amp=0.5)
    pen.stroke([(x - 8 * s, by + 4 * s), (x - 20 * s, g)], width=LINE * 1.5)
    pen.ellipse(x + 2 * s, by + 3 * s, 4 * s, 3 * s, start=0, end=180, width=1.1)


def camel(pen: Pen, x: float, g: float, s: float) -> None:
    tan = (214, 176, 118)
    body_w, body_h = 32 * s, 14 * s
    by = g - 18 * s
    pen.ellipse(x, by, body_w / 2, body_h / 2, fill=tan)
    pen.ellipse(x - 7 * s, by - 6 * s, 6 * s, 6 * s, start=180, end=360, fill=tan)
    pen.ellipse(x + 6 * s, by - 6 * s, 6 * s, 6 * s, start=180, end=360, fill=tan)
    pen.legs(x - body_w * 0.35, x + body_w * 0.35, by + body_h * 0.3, g, swing=2.5 * s)
    nx = x + body_w * 0.45
    pen.stroke([(nx, by - 2 * s), (nx + 5 * s, by - 16 * s), (nx + 10 * s, by - 18 * s)], width=LINE * 1.3, amp=0.7)
    pen.ellipse(nx + 12 * s, by - 18 * s, 5 * s, 3 * s, fill=tan)
    pen.dot(nx + 13 * s, by - 19 * s, 0.8 * s)
    pen.stroke([(x - body_w * 0.5, by), (x - body_w * 0.6, by + 9 * s)], width=1.2)


def hippo(pen: Pen, x: float, g: float, s: float) -> None:
    hide = (150, 140, 170)
    body_w, body_h = 36 * s, 20 * s
    by = g - 14 * s
    pen.ellipse(x, by, body_w / 2, body_h / 2, fill=hide)
    pen.legs(x - body_w * 0.32, x + body_w * 0.32, by + body_h * 0.3, g, w=LINE * 1.4, swing=1.8 * s)
    hx, hy = x + body_w * 0.5, by + 1 * s
    pen.ellipse(hx, hy, 11 * s, 7 * s, fill=hide)
    pen.stroke([(hx + 1 * s, hy + 1 * s), (hx + 11 * s, hy + 1 * s)], width=1.2)
    pen.dot(hx + 2 * s, hy - 3 * s, 0.9 * s)
    pen.ellipse(hx - 1 * s, hy - 7 * s, 1.6 * s, 1.6 * s, width=0.9, fill=hide)
    pen.ellipse(hx + 4 * s, hy - 7 * s, 1.6 * s, 1.6 * s, width=0.9, fill=hide)


def sheep(pen: Pen, x: float, g: float, s: float) -> None:
    by = g - 12 * s
    pen.scribble(x, by, 14 * s, 9 * s, turns=6, width=1.1, fill=PAPER)
    pen.legs(x - 8 * s, x + 8 * s, by + 6 * s, g, w=1.4, swing=2.0 * s)
    pen.ellipse(x + 14 * s, by - 2 * s, 4.5 * s, 3.5 * s, fill=INK)
    pen.dot(x + 15 * s, by - 3 * s, 0.9 * s, fill=PAPER)


def crocodile(pen: Pen, x: float, g: float, s: float) -> None:
    green = (110, 160, 80)
    pen.shape([(x - 22 * s, g - 1 * s), (x - 10 * s, g - 8 * s), (x + 10 * s, g - 8 * s),
               (x + 26 * s, g - 6 * s), (x + 26 * s, g - 1 * s)], fill=green, amp=0.8)
    pen.stroke([(x + 10 * s, g - 4 * s), (x + 26 * s, g - 4 * s)], width=1.1)
    for k in range(5):
        tx = x + 12 * s + k * 3 * s
        pen.shape([(tx, g - 4 * s), (tx + 1.5 * s, g - 6.5 * s), (tx + 3 * s, g - 4 * s)], fill=PAPER, width=0.8, amp=0.2)
    pen.dot(x + 14 * s, g - 9 * s, 1.2 * s)
    for k in range(4):
        bx = x - 8 * s + k * 5 * s
        pen.stroke([(bx, g - 8 * s), (bx + 2 * s, g - 11 * s), (bx + 4 * s, g - 8 * s)], width=0.9, amp=0.2)


def pig(pen: Pen, x: float, g: float, s: float) -> None:
    by = g - 9 * s
    pen.ellipse(x, by, 12 * s, 8 * s, fill=PINK)
    pen.legs(x - 7 * s, x + 7 * s, by + 5 * s, g, w=1.4, swing=2.0 * s)
    pig_head(pen, x + 13 * s, by - 2 * s, s)
    tail = [(x - 12 * s - 1.2 * s * i, by - 3 * s + 2.5 * s * math.sin(i * 1.6)) for i in range(6)]
    pen.stroke(tail, width=1.1, amp=0.3)


def pig_head(pen: Pen, x: float, y: float, s: float) -> None:
    pen.ellipse(x, y, 5 * s, 4 * s, fill=PINK)
    pen.ellipse(x + 4 * s, y + 1 * s, 2.2 * s, 1.8 * s, width=1.0, fill=(220, 120, 150))
    pen.dot(x + 3.4 * s, y + 1 * s, 0.5 * s)
    pen.dot(x + 4.8 * s, y + 1 * s, 0.5 * s)
    pen.dot(x - 1 * s, y - 1.5 * s, 0.8 * s)
    pen.stroke([(x - 2 * s, y - 4 * s), (x, y - 7 * s)], width=1.1)
    pen.stroke([(x + 1 * s, y - 4 * s), (x + 2.5 * s, y - 7 * s)], width=1.1)


def flying_pig(pen: Pen, x: float, y: float, s: float, t: float) -> None:
    flap = math.sin(TAU * t * 3)
    pen.ellipse(x - 2 * s, y - 6 * s, 8 * s, 3 * s, fill=PAPER, rot=-25 - flap * 20)
    pen.ellipse(x, y, 12 * s, 8 * s, fill=PINK)
    pig_head(pen, x + 13 * s, y - 2 * s, s)
    for k in range(4):
        lx = x - 6 * s + k * 4 * s
        pen.stroke([(lx, y + 6 * s), (lx - 1 * s, y + 11 * s)], width=1.2, amp=0.3)
    pen.ellipse(x - 4 * s, y - 8 * s, 9 * s, 3.2 * s, fill=PAPER, rot=-35 - flap * 22)
    tail = [(x - 12 * s - 1.2 * s * i, y - 3 * s + 2.5 * s * math.sin(i * 1.6)) for i in range(6)]
    pen.stroke(tail, width=1.1, amp=0.3)


def dog(pen: Pen, x: float, g: float, s: float) -> None:
    fur = (170, 120, 70)
    by = g - 10 * s
    pen.ellipse(x, by, 11 * s, 6 * s, fill=fur)
    pen.legs(x - 7 * s, x + 7 * s, by + 4 * s, g, w=1.4, swing=2.2 * s)
    pen.ellipse(x + 12 * s, by - 5 * s, 5 * s, 4 * s, fill=fur)
    pen.stroke([(x + 9 * s, by - 8 * s), (x + 7 * s, by - 2 * s)], width=1.3)
    pen.dot(x + 16 * s, by - 5.5 * s, 0.9 * s)
    pen.dot(x + 13 * s, by - 6 * s, 0.7 * s)
    pen.stroke([(x - 11 * s, by - 2 * s), (x - 16 * s, by - 9 * s + 2 * s * math.sin(TAU * pen.gait * 2))], width=1.3)


def cat(pen: Pen, x: float, g: float, s: float) -> None:
    fur = (230, 160, 80)
    by = g - 8 * s
    pen.ellipse(x, by, 9 * s, 5.5 * s, fill=fur)
    pen.legs(x - 5 * s, x + 5 * s, by + 3 * s, g, count=3, w=1.2, swing=2.0 * s)
    pen.ellipse(x + 10 * s, by - 5 * s, 4 * s, 3.6 * s, fill=fur)
    pen.shape([(x + 7 * s, by - 8 * s), (x + 8 * s, by - 11.5 * s), (x + 9.5 * s, by - 8 * s)], fill=fur, width=0.9, amp=0.2)
    pen.shape([(x + 11 * s, by - 8 * s), (x + 12.5 * s, by - 11.5 * s), (x + 13.5 * s, by - 7.5 * s)], fill=fur, width=0.9, amp=0.2)
    pen.dot(x + 9 * s, by - 5.5 * s, 0.6 * s)
    pen.dot(x + 12 * s, by - 5.5 * s, 0.6 * s)
    tail = [(x - 9 * s - 4 * s * math.sin(i * 0.7 + TAU * pen.gait), by - 2 * s - i * 2 * s) for i in range(7)]
    pen.stroke(tail, width=1.2, amp=0.4)


def ostrich(pen: Pen, x: float, g: float, s: float) -> None:
    by = g - 22 * s
    pen.scribble(x, by, 9 * s, 7 * s, turns=5, width=1.1, fill=(70, 70, 76))
    pen.stroke([(x - 3 * s, by + 6 * s), (x - 4 * s - 2 * s * math.sin(TAU * pen.gait), g)], width=1.3)
    pen.stroke([(x + 3 * s, by + 6 * s), (x + 4 * s + 2 * s * math.sin(TAU * pen.gait), g)], width=1.3)
    pen.stroke([(x + 6 * s, by - 4 * s), (x + 10 * s, by - 22 * s)], width=LINE)
    pen.ellipse(x + 11 * s, by - 24 * s, 3.5 * s, 2.5 * s, fill=PAPER)
    pen.stroke([(x + 14 * s, by - 24 * s), (x + 19 * s, by - 23 * s)], width=1.1, fill=(230, 150, 40))
    pen.dot(x + 11.5 * s, by - 25 * s, 0.7 * s)


def bear(pen: Pen, x: float, g: float, s: float) -> None:
    fur = (120, 85, 55)
    by = g - 16 * s
    pen.ellipse(x, by, 16 * s, 12 * s, fill=fur)
    pen.legs(x - 10 * s, x + 10 * s, by + 8 * s, g, w=LINE * 1.3, swing=2.0 * s)
    pen.ellipse(x + 16 * s, by - 6 * s, 7 * s, 6 * s, fill=fur)
    pen.ellipse(x + 12 * s, by - 11 * s, 2 * s, 2 * s, width=1.0, fill=fur)
    pen.ellipse(x + 19 * s, by - 11 * s, 2 * s, 2 * s, width=1.0, fill=fur)
    pen.ellipse(x + 21 * s, by - 4 * s, 3 * s, 2.2 * s, width=1.0, fill=(180, 140, 100))
    pen.dot(x + 23 * s, by - 4.5 * s, 0.9 * s)
    pen.dot(x + 17 * s, by - 7 * s, 0.8 * s)


def flamingo(pen: Pen, x: float, g: float, s: float) -> None:
    by = g - 26 * s
    pen.ellipse(x, by, 8 * s, 5 * s, fill=(240, 130, 160))
    pen.stroke([(x + 1 * s, by + 4 * s), (x + 1 * s, g)], width=1.2)
    pen.stroke([(x - 2 * s, by + 4 * s), (x - 5 * s + 4 * s * math.sin(TAU * pen.gait), by + 14 * s)], width=1.2)
    pen.stroke([(x + 6 * s, by - 3 * s), (x + 8 * s, by - 16 * s), (x + 11 * s, by - 22 * s)], width=LINE * 1.2, amp=0.6)
    pen.ellipse(x + 12 * s, by - 23 * s, 3.5 * s, 2.6 * s, fill=(240, 130, 160))
    pen.stroke([(x + 15 * s, by - 23 * s), (x + 19 * s, by - 20 * s)], width=1.3)
    pen.dot(x + 12.5 * s, by - 24 * s, 0.6 * s)


def mouse(pen: Pen, x: float, g: float, s: float) -> None:
    pen.ellipse(x, g - 3 * s, 5 * s, 3 * s, fill=(200, 200, 205))
    pen.ellipse(x + 5 * s, g - 5 * s, 1.5 * s, 1.5 * s, width=0.9, fill=(200, 200, 205))
    pen.dot(x + 5 * s, g - 3.6 * s, 0.5 * s)
    pen.stroke([(x - 5 * s, g - 3 * s), (x - 12 * s, g - 1 * s + 1.5 * s * math.sin(TAU * pen.gait))], width=1.0, amp=0.4)


def dove(pen: Pen, x: float, y: float, s: float, t: float) -> None:
    flap = math.sin(TAU * t * 4) * 4 * s
    pen.stroke([(x - 9 * s, y - 4 * s - flap), (x - 3 * s, y), (x + 2 * s, y - 5 * s - flap)], width=1.4, amp=0.4)
    pen.stroke([(x + 4 * s, y - 2 * s), (x + 8 * s, y + 1 * s)], width=1.2, amp=0.3)
    pen.stroke([(x + 8 * s, y + 1 * s), (x + 12 * s, y - 1 * s)], width=1.0, fill=(96, 150, 70))


ANIMALS = {
    "giraffe": (giraffe, 30), "elephant": (elephant, 60), "lion": (lion, 52), "zebra": (zebra, 52),
    "penguin": (penguin, 22), "snake": (snake, 46), "rabbit": (rabbit, 28), "turtle": (turtle, 32),
    "monkey": (monkey, 30), "kangaroo": (kangaroo, 44), "camel": (camel, 58), "hippo": (hippo, 58),
    "sheep": (sheep, 40), "crocodile": (crocodile, 56), "pig": (pig, 36), "dog": (dog, 36),
    "cat": (cat, 32), "ostrich": (ostrich, 34), "bear": (bear, 48), "flamingo": (flamingo, 30),
    "mouse": (mouse, 22),
}

QUEUE = ["elephant", "camel", "hippo", "zebra", "lion", "bear", "kangaroo", "ostrich",
         "crocodile", "sheep", "pig", "dog", "flamingo", "monkey", "cat", "turtle",
         "rabbit", "penguin", "snake", "mouse"]


LANES = (
    ((-30.0, 312.0), (380.0, 314.0), (540.0, HORIZON + 66)),
    ((-30.0, 266.0), (340.0, 262.0), (536.0, HORIZON + 62)),
)


def path_point(u: float, lane: int = 0) -> tuple[float, float, float]:
    (x0, y0), (cx, cy), (x1, y1) = LANES[lane]
    v = 1.0 - u
    x = v * v * x0 + 2 * v * u * cx + u * u * x1
    y = v * v * y0 + 2 * v * u * cy + u * u * y1
    depth = (y - HORIZON) / (312.0 - HORIZON)
    scale = 0.36 + 0.66 * depth
    return x, y, scale


def path_length(lane: int) -> float:
    total = 0.0
    px, py, _ = path_point(0.0, lane)
    for i in range(1, 201):
        x, y, _ = path_point(i / 200, lane)
        total += math.hypot(x - px, y - py)
        px, py = x, y
    return total


PATH_LENGTHS = tuple(path_length(lane) for lane in range(len(LANES)))


def gangplank(pen: Pen, x0: float, y0: float, x1: float, y1: float) -> None:
    pen.stroke([(x0, y0), (x1, y1)], width=LINE * 1.2, amp=0.8)
    pen.stroke([(x0 + 3, y0 + 5), (x1 + 3, y1 + 5)], width=LINE, amp=0.8)
    for k in range(7):
        t = (k + 0.5) / 7
        x = x0 + (x1 - x0) * t
        y = y0 + (y1 - y0) * t
        pen.stroke([(x, y), (x + 3, y + 5)], width=0.9, amp=0.3)


def stanchions(pen: Pen) -> None:
    posts = []
    for k in range(9):
        u = 0.04 + k * 0.118
        x, y, s = path_point(u, 0)
        posts.append((x - 4 * s, y - 15 * s, s * 0.85))
    for (x0, y0, s0), (x1, y1, s1) in zip(posts, posts[1:]):
        mid = ((x0 + x1) / 2, (y0 + y1) / 2 - 8 * (s0 + s1) / 2 + 4 * (s0 + s1) / 2)
        pts = [(x0, y0 - 12 * s0)]
        for k in range(1, 8):
            tt = k / 8
            sag = math.sin(math.pi * tt) * 4 * (s0 + s1) / 2
            pts.append((x0 + (x1 - x0) * tt, (y0 - 12 * s0) + ((y1 - 12 * s1) - (y0 - 12 * s0)) * tt + sag))
        pts.append((x1, y1 - 12 * s1))
        pen.stroke(pts, width=0.9, amp=0.2)
    for x, y, s in posts:
        pen.stroke([(x, y), (x, y - 13 * s)], width=1.3, amp=0.2)
        pen.ellipse(x, y, 4 * s, 1.6 * s, fill=PAPER, width=1.0)
        pen.dot(x, y - 13 * s, 1.4 * s)


def queue(pen: Pen, t: float) -> None:
    placements = []
    for lane in range(len(LANES)):
        u = 0.985 - lane * 0.06
        previous = 0.0
        for name in reversed(QUEUE[lane::len(LANES)]):
            fn, width = ANIMALS[name]
            x, y, s = path_point(u, lane)
            half = width * s * 0.55
            u -= (previous + half) / PATH_LENGTHS[lane]
            if u < -0.02:
                break
            x, y, s = path_point(u, lane)
            placements.append((y, name, x, s))
            previous = width * s * 0.55 + 6 * s
    placements.sort(key=lambda p: p[0])
    fence_drawn = False
    for i, (y, name, x, s) in enumerate(placements):
        if not fence_drawn and y >= 248:
            stanchions(pen)
            fence_drawn = True
        fn, width = ANIMALS[name]
        rng = np.random.default_rng(i * 7 + 3)
        phase = rng.uniform(0, 1)
        pen.gait = (t * 2 + phase + 0.3) % 1.0
        fn(pen, x + width * s * 0.36, y - 9 * s - abs(math.sin(TAU * pen.gait)) * 1.4 * s, s * 0.82)
        pen.gait = (t * 2 + phase) % 1.0
        fn(pen, x, y - abs(math.sin(TAU * pen.gait)) * 1.6 * s, s)
    pen.gait = 0.0


def speech(pen: Pen, x: float, y: float, tail_to, lines, size: float = 10.0) -> None:
    widest = max(text_width(line, size) for line in lines)
    for i, text in enumerate(lines):
        w = text_width(text, size)
        letter(pen, text, x - w / 2, y + i * (size * 1.45), size)
    tx, ty = tail_to
    bottom = y + (len(lines) - 1) * size * 1.45 + 4
    sx = x + (widest / 2 - 4) * (1 if tx > x else -1)
    pen.stroke([(sx, bottom - size * 0.6), (tx, ty)], width=0.9, amp=0.3)


def draw_frame(version: str, t: float, boil: int) -> Image.Image:
    image = Image.new("RGB", (WIDTH * SS, HEIGHT * SS), PAPER)
    pen = Pen(image, SEED + boil)
    sun(pen, 838, 58, 22, t)
    mountains(pen)
    cloud(pen, 330, 112, 100, 20, bumps=4)
    cloud(pen, 590, 122, 80, 16, bumps=4)
    cloud(pen, 730, 36, 150, 30, bumps=6, dark=True)
    rain(pen, 730, 36, 140, 54, 56, t)
    waves(pen, t)
    shore(pen)
    ark_x, ark_y = 700.0, HORIZON + 52
    bob = 3.0 * math.sin(TAU * t)
    roll = 1.6 * math.sin(TAU * t + 1.2)
    ark(pen, ark_x, ark_y + bob, 0.62, roll, t)
    plank_x1, plank_y1 = ark_x - 76, ark_y + bob - 18
    px0, py0, _ = path_point(1.0, 0)
    gangplank(pen, px0, py0, plank_x1, plank_y1)
    queue(pen, t)
    stick_figure(pen, px0 + 14, py0 + 4, h=30, facing=-1, clipboard=True)
    speech(pen, 470, 80, (612, 152), ["Two of everything.", "Yes. Even the mosquitoes."])
    speech(pen, 462, 172, (544, 218), ["The unicorns said", "they'd catch the next one."], size=9.0)
    flying_pig(pen, 118 + 6 * math.sin(TAU * t), 122 + 4 * math.sin(TAU * t * 2), 1.2, t)
    dove(pen, 520 - ((t + 0.10) % 1.0) * 600, 136 + 4 * math.sin(TAU * t * 2), 1.5, t)
    dove(pen, 520 - ((t + 0.18) % 1.0) * 600, 142 + 4 * math.sin(TAU * t * 2 + 1), 1.1, t + 0.1)
    title(pen, version)
    panel(pen)
    return image.resize((int(WIDTH * OUT_SCALE), int(HEIGHT * OUT_SCALE)), Image.Resampling.LANCZOS)


def main() -> None:
    version = project_version()
    output = ROOT / "data" / f"about-splash-{version}.png"
    draw_frame(version, 0.0, 0).save(output, optimize=True)
    print(f"wrote {output}")


if __name__ == "__main__":
    main()
