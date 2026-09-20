#!/usr/bin/env python3
# gen-splash.py — render the about:start splash animation and embed it as src/about_splash_gif.h.
import base64
import math
import os
import re
import shutil
import subprocess
import sys
import textwrap
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 940, 320
S = 3
FRAMES = int(os.environ.get("NS_SPLASH_FRAMES", "32"))
DELAY_MS = int(os.environ.get("NS_SPLASH_DELAY_MS", "80"))
LOSSY = int(os.environ.get("NS_SPLASH_LOSSY", "60"))
TAGLINE = "Yet another open source web browser"
BUBBLE = ["TWO OF EACH.", "YES, EVEN BROWSERS."]

INK = (0, 0, 0)
PAPER = (255, 255, 255)
SUN = (255, 214, 64)
WATER = (52, 120, 200)
WOOD = (214, 180, 128)
RED = (214, 64, 56)
LINE = 2.0
GROUND_Y = 252.0
SHORE_X = 580.0


def version():
    text = open(os.path.join(ROOT, "meson.build")).read()
    m = re.search(r"^\s*version:\s*'([^']*)'", text, re.M)
    if not m:
        sys.exit("could not read version from meson.build")
    return m.group(1).split("-")[0]


def find_font(query, *paths):
    for p in paths:
        if os.path.isfile(p):
            return p
    if shutil.which("fc-match"):
        f = subprocess.run(["fc-match", "-f", "%{file}", query],
                           capture_output=True, text=True).stdout.strip()
        if f and os.path.isfile(f):
            return f
    win_fonts = os.environ.get("WINDIR", "C:\\Windows") + "\\Fonts"
    for fn in ("comicbd.ttf", "comic.ttf", "LiberationSans-Bold.ttf", "LiberationSans-Regular.ttf",
               "DejaVuSans-Bold.ttf", "DejaVuSans.ttf", "arialbd.ttf", "arial.ttf"):
        p = os.path.join(win_fonts, fn)
        if os.path.isfile(p):
            return p
    sys.exit("missing font: " + query)


def sx(v):
    return v * S


def densify(pts, spacing=2.5):
    out = [pts[0]]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        d = math.hypot(x1 - x0, y1 - y0)
        n = max(1, int(d / spacing))
        for i in range(1, n + 1):
            u = i / n
            out.append((x0 + (x1 - x0) * u, y0 + (y1 - y0) * u))
    return out


def wobble(pts, seed, amp):
    n = len(pts)
    if n < 3 or amp <= 0:
        return pts
    rng = np.random.default_rng(seed)
    noise = rng.normal(0.0, 1.0, n + 8)
    kernel = np.ones(5) / 5.0
    noise = np.convolve(noise, kernel, mode="same")[3:3 + n] * amp * 1.5
    out = []
    for i, (x, y) in enumerate(pts):
        x0, y0 = pts[max(0, i - 1)]
        x1, y1 = pts[min(n - 1, i + 1)]
        tx, ty = x1 - x0, y1 - y0
        length = math.hypot(tx, ty) or 1.0
        nx, ny = -ty / length, tx / length
        out.append((x + nx * noise[i], y + ny * noise[i]))
    return out


def circle(cx, cy, r, n=36, a0=0.0, a1=2 * math.pi, ry=None):
    ry = r if ry is None else ry
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / n),
             cy + ry * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


def star(cx, cy, r, points=5):
    out = []
    for i in range(points * 2):
        rr = r if i % 2 == 0 else r * 0.45
        a = -math.pi / 2 + math.pi * i / points
        out.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return out


class Ink:
    def __init__(self, fonts):
        self.img = Image.new("RGB", (W * S, H * S), PAPER)
        self.d = ImageDraw.Draw(self.img)
        self.fonts = fonts
        self.seed = 1

    def start(self, name):
        self.seed = zlib.crc32(name.encode()) & 0xffff

    def stroke(self, pts, width=LINE, amp=0.8, closed=False, fill=None, color=INK):
        if closed:
            pts = list(pts) + [pts[0]]
        pts = densify(pts)
        self.seed += 1
        pts = wobble(pts, self.seed, amp)
        sp = [(sx(x), sx(y)) for x, y in pts]
        if fill is not None:
            self.d.polygon(sp, fill=fill)
        w = max(1, int(round(sx(width))))
        self.d.line(sp, fill=color, width=w, joint="curve")
        r = w / 2.0
        for x, y in (sp[0], sp[-1]):
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=color)

    def dot(self, x, y, r=1.5):
        self.d.ellipse([sx(x - r), sx(y - r), sx(x + r), sx(y + r)], fill=INK)

    def text(self, x, y, s, size, bold=True, anchor="la"):
        font = ImageFont.truetype(self.fonts[0] if bold else self.fonts[1], int(sx(size)))
        self.d.text((sx(x), sx(y)), s, font=font, fill=INK, anchor=anchor)

    def text_width(self, s, size, bold=True):
        font = ImageFont.truetype(self.fonts[0] if bold else self.fonts[1], int(sx(size)))
        return self.d.textlength(s, font=font) / S


class Place:
    def __init__(self, ox, oy, scale=1.0, flip=False, rot=0.0):
        self.ox, self.oy, self.scale, self.flip, self.rot = ox, oy, scale, flip, rot

    def __call__(self, pts):
        f = -1.0 if self.flip else 1.0
        c, s = math.cos(self.rot), math.sin(self.rot)
        out = []
        for x, y in pts:
            x, y = f * x * self.scale, y * self.scale
            out.append((self.ox + x * c - y * s, self.oy + x * s + y * c))
        return out

    def p(self, x, y):
        return self(((x, y),))[0]

    def lw(self, k=1.0):
        return LINE * k * (0.6 + 0.4 * self.scale)


def gait(phase, k=2):
    return math.sin(2 * math.pi * phase * k)


def legs_2d(ink, t, xs, top, phase, swing=4.0):
    for i, x in enumerate(xs):
        s = swing * math.sin(2 * math.pi * phase * 2 + (0 if i % 2 == 0 else math.pi))
        ink.stroke(t([(x, top), (x + s * 0.5, top * 0.5), (x + s, 0)]), width=t.lw())


def body_2d(ink, t, cx, cy, rx, ry):
    ink.stroke(t(circle(cx, cy, rx, 32, ry=ry)), fill=PAPER, width=t.lw())


def head_2d(ink, t, cx, cy, r, eye=True):
    ink.stroke(t(circle(cx, cy, r, 24)), fill=PAPER, width=t.lw())
    if eye:
        ink.dot(*t.p(cx + r * 0.35, cy - r * 0.25), 1.1 * t.scale)


def elephant_2d(ink, t, phase):
    bob = 0.8 * gait(phase)
    legs_2d(ink, t, [10, 18, 36, 44], -22, phase, 2.5)
    body_2d(ink, t, 28, -36 + bob, 26, 17)
    ink.stroke(t(circle(39, -40 + bob, 8, 20, ry=10)), fill=PAPER, width=t.lw())
    ink.stroke(t(circle(44, -41 + bob, 10, 24, math.radians(110), math.radians(390))), fill=PAPER, width=t.lw())
    swing = 3.5 * gait(phase, 1)
    ink.stroke(t([(52, -36 + bob), (59, -28 + bob), (60 + swing, -17 + bob), (57 + swing, -7 + bob),
                  (60 + swing * 1.5, -2)]), width=t.lw(1.2))
    ink.dot(*t.p(49, -44 + bob), 1.2 * t.scale)
    ink.stroke(t([(2, -38 + bob), (-4, -30 + bob)]), width=t.lw())
    m_ox, m_oy = 24, -53 + bob
    ink.stroke(t(circle(m_ox, m_oy - 4, 4.0, 14)), fill=PAPER, width=t.lw(0.8))
    ink.stroke(t(circle(m_ox + 4, m_oy - 9, 3.0, 12)), fill=PAPER, width=t.lw(0.8))
    ink.dot(*t.p(m_ox + 5, m_oy - 9), 0.8 * t.scale)
    ink.stroke(t([(m_ox - 3, m_oy - 2), (m_ox - 8, m_oy - 7)]), width=t.lw(0.7))


def giraffe_2d(ink, t, phase):
    legs_2d(ink, t, [6, 11, 21, 26], -30, phase, 3.0)
    body_2d(ink, t, 16, -34, 15, 9)
    sway = 1.8 * gait(phase, 1)
    ink.stroke(t([(21, -34), (27 + sway, -52), (30 + sway * 1.5, -68)]), width=t.lw(1.2))
    hx, hy = 33 + sway * 1.5, -73
    ink.stroke(t(circle(hx, hy, 4.0, 14)), fill=PAPER, width=t.lw())
    ink.stroke(t([(hx - 1, hy - 4), (hx - 2, hy - 8)]), width=t.lw(0.8))
    ink.dot(*t.p(hx - 2, hy - 8), 1.0 * t.scale)
    ink.stroke(t([(hx + 2, hy - 4), (hx + 3, hy - 8)]), width=t.lw(0.8))
    ink.dot(*t.p(hx + 3, hy - 8), 1.0 * t.scale)
    ink.dot(*t.p(hx + 2, hy), 0.9 * t.scale)
    for sx_i, sy_i in ((9, -31), (17, -28), (13, -34), (25 + sway * 0.5, -46), (28 + sway * 1.0, -58)):
        ink.stroke(t(circle(sx_i, sy_i, 1.6, 8)), width=t.lw(0.7), amp=0.2)


def kangaroo_2d(ink, t, phase):
    hop = abs(math.sin(2 * math.pi * phase * 2)) * 4.5
    tk = Place(t.ox, t.oy - hop * t.scale, t.scale, t.flip, t.rot)
    ink.stroke(tk([(6, -16), (-2, -10), (-8, -3)]), width=t.lw(1.1))
    ink.stroke(tk([(9, -5), (14, -1), (18, 0)]), width=t.lw(1.0))
    ink.stroke(tk(circle(11, -18, 8, 18, ry=10, a0=-math.pi * 0.2, a1=math.pi * 1.1)), fill=PAPER, width=t.lw())
    ink.stroke(tk([(13, -27), (17, -37)]), width=t.lw(1.0))
    ink.stroke(tk(circle(20, -40, 3.8, 14)), fill=PAPER, width=t.lw())
    ink.stroke(tk([(18, -44), (17, -48)]), width=t.lw(0.8))
    ink.stroke(tk([(21, -44), (22, -48)]), width=t.lw(0.8))
    ink.dot(*tk.p(21, -40), 0.9 * t.scale)
    ink.stroke(tk([(15, -21), (19, -19)]), width=t.lw())


def sheep_2d(ink, t, phase):
    bob = 0.6 * gait(phase)
    legs_2d(ink, t, [5, 9, 18, 22], -12, phase, 2.5)
    body_2d(ink, t, 14, -17 + bob, 12, 8)
    for cx_i, cy_i in ((6, -18), (10, -22), (16, -22), (21, -19), (18, -13), (11, -13)):
        ink.stroke(t(circle(cx_i, cy_i + bob, 3.5, 10)), fill=PAPER, width=t.lw(0.7), amp=0.3)
    head_2d(ink, t, 24, -20 + bob, 3.5)
    ink.stroke(t([(24, -23 + bob), (28, -21 + bob)]), width=t.lw(0.8))


def dog_2d(ink, t, phase):
    bob = 0.8 * gait(phase)
    legs_2d(ink, t, [4, 8, 16, 20], -11, phase, 3.0)
    body_2d(ink, t, 12, -15 + bob, 11, 6)
    head_2d(ink, t, 20, -20 + bob, 3.8)
    ink.stroke(t([(19, -23 + bob), (17, -19 + bob)]), width=t.lw())
    wag = 3.0 * math.sin(2 * math.pi * phase * 4)
    ink.stroke(t([(1, -16 + bob), (-3, -22 + bob + wag)]), width=t.lw())


def pig_2d(ink, t, phase):
    bob = 0.5 * gait(phase)
    legs_2d(ink, t, [4, 8, 15, 19], -9, phase, 2.2)
    body_2d(ink, t, 12, -14 + bob, 11, 7)
    head_2d(ink, t, 20, -15 + bob, 3.8)
    ink.stroke(t([(23, -15 + bob), (25, -15 + bob)]), width=t.lw(1.2))
    curl = circle(0, -15 + bob, 2.5, 12, a0=0, a1=3 * math.pi)
    ink.stroke(t(curl), width=t.lw(0.7))


def cat_2d(ink, t, phase):
    bob = 0.7 * gait(phase)
    legs_2d(ink, t, [3, 6, 12, 15], -9, phase, 2.8)
    body_2d(ink, t, 9, -12 + bob, 8, 5)
    head_2d(ink, t, 16, -16 + bob, 3.2)
    ink.stroke(t([(14, -19 + bob), (14, -22 + bob), (16, -19 + bob)]), width=t.lw(0.8))
    ink.stroke(t([(17, -19 + bob), (18, -22 + bob), (19, -18 + bob)]), width=t.lw(0.8))
    sway = 2.5 * gait(phase, 1)
    ink.stroke(t([(1, -12 + bob), (-3, -18 + bob), (-2 + sway, -23 + bob)]), width=t.lw(0.8))


def penguin_2d(ink, t, phase):
    waddle = 2.0 * gait(phase)
    tp = Place(t.ox, t.oy, t.scale, t.flip, t.rot + math.radians(waddle))
    body_2d(ink, tp, 0, -11, 5, 9)
    head_2d(ink, tp, 0, -21, 3.5)
    ink.stroke(tp([(3, -21), (7, -20)]), width=tp.lw())
    ink.stroke(tp([(-3, -13), (-6, -6)]), width=tp.lw())
    ink.stroke(tp([(0, -2), (3, 0)]), width=tp.lw(1.2))


def rabbit_2d(ink, t, phase):
    hop = abs(math.sin(2 * math.pi * phase * 2)) * 3.5
    tr = Place(t.ox, t.oy - hop * t.scale, t.scale, t.flip, t.rot)
    body_2d(ink, tr, 6, -7, 6, 5)
    head_2d(ink, tr, 11, -12, 3.0)
    ink.stroke(tr([(9, -14), (8, -21)]), width=tr.lw(0.8))
    ink.stroke(tr([(11, -14), (12, -21)]), width=tr.lw(0.8))
    ink.stroke(tr([(0, -7), (-3, -8)]), width=tr.lw(1.0))
    ink.stroke(tr([(4, -3), (3, 0)]), width=tr.lw())
    ink.stroke(tr([(9, -3), (12, 0)]), width=tr.lw())


def duck_2d(ink, t, phase):
    bob = 1.0 * gait(phase)
    body_2d(ink, t, 6, -7 + bob, 6, 4)
    head_2d(ink, t, 11, -12 + bob, 2.8)
    ink.stroke(t([(13, -12 + bob), (17, -11 + bob)]), width=t.lw(1.1))
    s = 2.0 * gait(phase)
    ink.stroke(t([(4, -3 + bob), (4 + s, 0)]), width=t.lw(0.8))
    ink.stroke(t([(8, -3 + bob), (8 - s, 0)]), width=t.lw(0.8))


def mouse_2d(ink, t, phase):
    ink.stroke(t(circle(5, -4, 5, 14, ry=3.2)), fill=PAPER, width=t.lw(0.8))
    ink.stroke(t(circle(10, -5, 2.5, 10)), fill=PAPER, width=t.lw(0.8))
    ink.dot(*t.p(11, -5.5), 0.8 * t.scale)
    ink.stroke(t(circle(8, -8, 1.8, 8)), fill=PAPER, width=t.lw(0.7))
    flick = 1.5 * gait(phase, 1)
    ink.stroke(t([(0, -3), (-4, -2 + flick), (-8, -5 + flick)]), width=t.lw(0.6))


def browser_2d(ink, t, phase):
    bob = 0.6 * gait(phase)
    top = -23 + bob
    s = 2.8 * gait(phase)
    ink.stroke(t([(5, -7 + bob), (4 + s, 0)]), width=t.lw(0.8))
    ink.stroke(t([(13, -7 + bob), (14 - s, 0)]), width=t.lw(0.8))
    ink.stroke(t([(0, top), (18, top), (18, -7 + bob), (0, -7 + bob)]), closed=True, fill=PAPER,
               width=t.lw(0.9), amp=0.3)
    ink.stroke(t([(0, top + 4.5), (18, top + 4.5)]), width=t.lw(0.6), amp=0.15)
    for dx in (2.2, 4.8, 7.4):
        ink.dot(*t.p(dx, top + 2.2), 0.65 * t.scale)
    ink.stroke(t([(2, top + 7.5), (16, top + 7.5)]), width=t.lw(0.5), amp=0.15)
    ink.dot(*t.p(6, -12.5 + bob), 0.8 * t.scale)
    ink.dot(*t.p(12, -12.5 + bob), 0.8 * t.scale)
    ink.stroke(t([(6.5, -10 + bob), (9, -9 + bob), (11.5, -10 + bob)]), width=t.lw(0.5), amp=0.1)


def turtle_2d(ink, t, phase):
    ink.stroke(t(circle(7, -5, 7, 16, ry=5, a0=math.pi, a1=2 * math.pi)), fill=PAPER, width=t.lw(1.0))
    ink.stroke(t([(0, -5), (14, -5)]), width=t.lw(0.8))
    head_2d(ink, t, 17, -5, 2.2)
    s = 1.5 * gait(phase, 2)
    ink.stroke(t([(3, -4), (1 + s, 0)]), width=t.lw(0.8))
    ink.stroke(t([(11, -4), (13 - s, 0)]), width=t.lw(0.8))


ANIMALS_2D = [
    (elephant_2d, 46),
    (giraffe_2d, 30),
    (kangaroo_2d, 26),
    (sheep_2d, 28),
    (dog_2d, 30),
    (pig_2d, 28),
    (cat_2d, 26),
    (penguin_2d, 18),
    (rabbit_2d, 20),
    (duck_2d, 22),
    (mouse_2d, 14),
    (browser_2d, 22),
]


def procession_2d(ink, phase):
    curr_x = 530.0
    for k, (animal_fn, width) in enumerate(reversed(ANIMALS_2D)):
        curr_x -= width
        if curr_x < 20:
            continue
        ink.start(f"pair-{k}-back")
        animal_fn(ink, Place(curr_x - 6, GROUND_Y - 3, 0.94), phase + 0.35)
        ink.start(f"pair-{k}-front")
        animal_fn(ink, Place(curr_x, GROUND_Y, 1.0), phase)
        curr_x -= 16.0


def stick_figure_2d(ink, x, y, scale=1.0, wave=0.0, flip=False):
    t = Place(x, y, scale, flip)
    head_2d(ink, t, 0, -42, 6.5, eye=False)
    ink.stroke(t([(0, -36), (0, -14)]), width=t.lw(1.1))
    ink.stroke(t([(0, -14), (-6, 0)]), width=t.lw(1.1))
    ink.stroke(t([(0, -14), (6, 0)]), width=t.lw(1.1))
    ink.stroke(t([(0, -28), (-8, -20)]), width=t.lw(1.0))
    ink.stroke(t([(0, -28), (7, -34), (9 + wave, -44)]), width=t.lw(1.0))


def noah_2d(ink, phase, x=555, y=GROUND_Y):
    t = Place(x, y, 1.0)
    head_2d(ink, t, 0, -42, 6.5, eye=False)
    gust = 1.2 * gait(phase, 1)
    for bx in (-3, 0, 3):
        ink.stroke([(x + bx, y - 36), (x + bx + gust * 0.3, y - 29)], width=LINE * 0.7, amp=0.2)
    ink.stroke([(x, y - 36), (x, y - 14)], width=LINE * 1.1)
    ink.stroke([(x, y - 14), (x - 6, y)], width=LINE * 1.1)
    ink.stroke([(x, y - 14), (x + 6, y)], width=LINE * 1.1)
    ink.stroke([(x, y - 28), (x - 10, y - 22)], width=LINE * 1.0)
    ink.stroke([(x - 10, y - 48), (x - 10, y)], width=LINE * 1.1)
    ink.stroke([(x, y - 28), (x + 8, y - 24)], width=LINE * 1.0)
    cb_x, cb_y = x + 8, y - 28
    ink.stroke([(cb_x, cb_y), (cb_x + 8, cb_y - 2), (cb_x + 6, cb_y + 10), (cb_x - 2, cb_y + 12)],
               fill=PAPER, closed=True, width=LINE * 0.7, amp=0.1)
    ink.stroke([(cb_x + 3, cb_y - 3), (cb_x + 5, cb_y - 3)], width=LINE * 0.8)
    ink.stroke([(x + 2, y - 24), (cb_x + 3, cb_y + 4)], width=LINE * 0.7)


def ark_2d(ink, phase, ax=665, ay=GROUND_Y):
    bob = 1.5 * math.sin(2 * math.pi * phase)
    deck_y = ay - 35 + bob
    hull_bot = ay + 26 + bob

    hull = [(ax, deck_y), (ax + 16, hull_bot), (ax + 145, hull_bot), (ax + 172, deck_y)]
    ink.stroke(hull, fill=WOOD, width=LINE * 1.3, closed=True)
    for p_y in (deck_y + 16, deck_y + 32, deck_y + 46):
        if p_y < hull_bot:
            ink.stroke([(ax + 8, p_y), (ax + 162, p_y)], width=LINE * 0.7, amp=0.3)
    rail_y = deck_y - 8
    ink.stroke([(ax, rail_y), (ax + 172, rail_y)], width=LINE * 0.8, amp=0.2)
    for rx in range(int(ax + 10), int(ax + 172), 18):
        ink.stroke([(rx, deck_y), (rx, rail_y)], width=LINE * 0.6, amp=0.1)

    cab_x = ax + 34
    cab_w = 96
    cab_top = deck_y - 48
    ink.stroke([(cab_x, deck_y), (cab_x, cab_top), (cab_x + cab_w, cab_top), (cab_x + cab_w, deck_y)],
               fill=PAPER, closed=True, width=LINE * 1.1)
    roof_peak_x = cab_x + cab_w / 2
    roof_peak_y = cab_top - 20
    roof = [(cab_x - 6, cab_top), (roof_peak_x, roof_peak_y), (cab_x + cab_w + 6, cab_top)]
    ink.stroke(roof, fill=WOOD, width=LINE * 1.2, closed=True)

    flutter = 2.0 * gait(phase, 3)
    pole_top = roof_peak_y - 26
    ink.stroke([(roof_peak_x, roof_peak_y), (roof_peak_x, pole_top)], width=LINE * 0.9, amp=0.1)
    flag = [(roof_peak_x, pole_top), (roof_peak_x + 22, pole_top + 3 + flutter),
            (roof_peak_x, pole_top + 14)]
    ink.stroke(flag, fill=PAPER, closed=True, width=LINE * 0.8, amp=0.3)
    ink.stroke(star(roof_peak_x + 7, pole_top + 7 + flutter * 0.3, 4.0), fill=SUN, closed=True,
               width=LINE * 0.5, amp=0.1)

    ink.stroke([(cab_x + 12, deck_y), (cab_x + 12, deck_y - 28),
                (cab_x + 28, deck_y - 28), (cab_x + 28, deck_y)], width=LINE * 0.9)
    ink.dot(cab_x + 25, deck_y - 14, 1.0)

    for wx in (cab_x + 48, cab_x + 75):
        wy = deck_y - 24
        ink.stroke(circle(wx, wy, 7, 16), fill=PAPER, width=LINE * 0.8)
        ink.stroke([(wx - 7, wy), (wx + 7, wy)], width=LINE * 0.6)
        ink.stroke([(wx, wy - 7), (wx, wy + 7)], width=LINE * 0.6)

    ch_x = cab_x + cab_w - 18
    ink.stroke([(ch_x, cab_top - 12), (ch_x, cab_top - 30),
                (ch_x + 8, cab_top - 30), (ch_x + 8, cab_top - 8)], fill=PAPER, width=LINE * 0.8)
    smoke_y = cab_top - 34 - 4 * (phase % 0.5)
    ink.stroke(circle(ch_x + 4, smoke_y, 3.5, 10), width=LINE * 0.5, amp=0.2)
    ink.stroke(circle(ch_x + 8, smoke_y - 7, 5.0, 12), width=LINE * 0.4, amp=0.2)

    gx = cab_x + 36
    ink.stroke([(gx, cab_top - 10), (gx + 2, cab_top - 32)], width=LINE * 1.1)
    ink.stroke(circle(gx + 4, cab_top - 35, 3.5, 12), fill=PAPER, width=LINE * 0.8)
    ink.dot(gx + 5, cab_top - 35, 0.9)
    ink.stroke([(gx + 3, cab_top - 38), (gx + 3, cab_top - 42)], width=LINE * 0.7)
    ink.stroke([(gx + 6, cab_top - 38), (gx + 7, cab_top - 42)], width=LINE * 0.7)

    cx = cab_x + 78
    ink.stroke(circle(cx, roof_peak_y + 6, 3.0, 10), fill=PAPER, width=LINE * 0.7)
    ink.stroke(circle(cx, roof_peak_y + 11, 4.5, 12), fill=PAPER, width=LINE * 0.7)
    ink.stroke([(cx + 4, roof_peak_y + 12), (cx + 8, roof_peak_y + 8)], width=LINE * 0.6)

    penguin_2d(ink, Place(ax + 165, deck_y, 0.7), phase)
    stick_figure_2d(ink, ax + 18, deck_y, 0.72, wave=2.5 * gait(phase, 2), flip=True)

    gp = [(SHORE_X - 5, ay - 2), (ax + 2, deck_y)]
    ink.stroke(gp, width=LINE * 1.3)
    gp_bot = [(SHORE_X - 5, ay + 2), (ax + 2, deck_y + 4)]
    ink.stroke(gp_bot, width=LINE * 0.8)
    for u in np.linspace(0.15, 0.9, 8):
        gx1 = SHORE_X - 5 + (ax - SHORE_X + 5) * u
        gy1 = (ay - 2) + (deck_y - ay + 2) * u
        ink.stroke([(gx1, gy1), (gx1 + 2, gy1 + 4)], width=LINE * 0.7, amp=0.1)

    turtle_2d(ink, Place(600, ay - 8, 0.55), phase)
    turtle_2d(ink, Place(635, ay - 18, 0.55), phase + 0.5)


def lighthouse_2d(ink, phase, lx=890, ly=GROUND_Y):
    rocks = [(lx - 28, ly + 25), (lx - 22, ly + 6), (lx - 12, ly - 2),
             (lx + 24, ly - 2), (lx + 30, ly + 25)]
    ink.stroke(rocks, fill=PAPER, width=LINE * 1.2)
    ink.stroke([(lx - 16, ly + 8), (lx - 6, ly + 4)], width=LINE * 0.6, amp=0.2)
    ink.stroke([(lx + 6, ly + 12), (lx + 18, ly + 6)], width=LINE * 0.6, amp=0.2)

    base_y = ly - 2
    deck_y = base_y - 88
    bw = 20.0
    tw = 12.0
    tower = [(lx - bw / 2, base_y), (lx - tw / 2, deck_y),
             (lx + tw / 2, deck_y), (lx + bw / 2, base_y)]
    ink.stroke(tower, fill=PAPER, closed=True, width=LINE * 1.2)

    for s_bot, s_top in ((base_y - 18, base_y - 32), (base_y - 48, base_y - 62), (base_y - 74, base_y - 84)):
        w1 = bw - (bw - tw) * ((base_y - s_bot) / 88)
        w2 = bw - (bw - tw) * ((base_y - s_top) / 88)
        ink.stroke([(lx - w1 / 2, s_bot), (lx - w2 / 2, s_top),
                    (lx + w2 / 2, s_top), (lx + w1 / 2, s_bot)],
                   fill=RED, closed=True, width=LINE * 0.7, amp=0.2)

    for wy in (base_y - 12, base_y - 42, base_y - 68):
        ink.stroke([(lx - 2, wy), (lx + 2, wy), (lx + 2, wy - 5), (lx - 2, wy - 5)],
                   fill=INK, closed=True, width=LINE * 0.5, amp=0.1)

    rw = tw + 8.0
    ink.stroke([(lx - rw / 2, deck_y), (lx + rw / 2, deck_y)], width=LINE * 1.2, amp=0.2)
    rail_y = deck_y - 8
    ink.stroke([(lx - rw / 2, rail_y), (lx + rw / 2, rail_y)], width=LINE * 0.7, amp=0.2)
    for px in (-rw / 2 + 1, -rw / 6, rw / 6, rw / 2 - 1):
        ink.stroke([(lx + px, deck_y), (lx + px, rail_y)], width=LINE * 0.6, amp=0.1)

    kx = lx - rw / 2 + 3
    ink.dot(kx, rail_y - 5, 1.2)
    ink.stroke([(kx, rail_y - 4), (kx, rail_y + 3)], width=LINE * 0.7, amp=0.2)
    wave_arm = 2.0 * math.sin(2 * math.pi * phase * 2)
    ink.stroke([(kx, rail_y - 2), (kx - 3, rail_y - 6 + wave_arm)], width=LINE * 0.6, amp=0.1)

    roof_base_y = deck_y - 16
    ink.stroke([(lx - tw / 2 + 1, deck_y), (lx - tw / 2 + 1, roof_base_y),
                (lx + tw / 2 - 1, roof_base_y), (lx + tw / 2 - 1, deck_y)],
               fill=PAPER, closed=True, width=LINE * 0.9, amp=0.2)
    ink.stroke([(lx, deck_y), (lx, roof_base_y)], width=LINE * 0.6, amp=0.1)
    ink.stroke(circle(lx, deck_y - 8, 2.6, 12), fill=SUN, width=LINE * 0.5, amp=0.1)

    apex_y = roof_base_y - 9
    ink.stroke([(lx - tw / 2 - 1, roof_base_y), (lx, apex_y), (lx + tw / 2 + 1, roof_base_y)],
               fill=RED, closed=True, width=LINE * 1.0, amp=0.2)
    ink.stroke([(lx, apex_y), (lx, apex_y - 6)], width=LINE * 0.8, amp=0.1)
    ink.dot(lx, apex_y - 6, 1.0)


def gull_2d(ink, x, y, flap, size=6.0):
    lift = size * 0.5 * flap
    ink.stroke([(x - size, y + lift * 0.4), (x - size * 0.5, y - lift), (x, y),
                (x + size * 0.5, y - lift), (x + size, y + lift * 0.4)],
               width=LINE * 0.6, amp=0.15)


def gulls_2d(ink, phase, cx=840, cy=150):
    for k, (r, ry, speed, size) in enumerate(((34, 12, 1, 6.0), (48, 16, -1, 5.0))):
        a = 2 * math.pi * (phase * speed + k * 0.4)
        x = cx + r * math.cos(a)
        y = cy + ry * math.sin(a) - k * 18
        gull_2d(ink, x, y, math.sin(2 * math.pi * phase * 4 + k), size)


def water_2d(ink, phase, start_x=595):
    for y_row, amp, speed in ((252, 1.2, 1), (262, 1.6, -1), (274, 2.0, 1),
                              (288, 2.4, -1), (302, 2.8, 1), (314, 3.0, -1)):
        pts = [(x, y_row + amp * math.sin(x / 14.0 + 2 * math.pi * phase * speed))
               for x in range(start_x, W + 10, 5)]
        ink.stroke(pts, width=LINE * 0.75, amp=0.15, color=WATER)


def whale_2d(ink, phase, wx=620, wy=292):
    rise = 0.5 - 0.5 * math.cos(2 * math.pi * phase)
    tw = Place(wx, wy + 8.0 * (1.0 - rise), 0.75)
    ink.stroke(tw([(0, 0), (14, -7), (32, -9), (50, -5), (60, 0)]), width=LINE * 1.1, fill=PAPER)
    ink.stroke(tw([(60, 0), (68, -8 - 4 * rise), (75, -3)]), width=LINE * 0.9)
    ink.dot(*tw.p(35, -6), 1.1)
    if rise > 0.4:
        spout = 4.0 + 10.0 * rise
        ink.stroke(tw([(24, -9), (20, -9 - spout)]), width=LINE * 0.8, amp=0.2, color=WATER)
        ink.stroke(tw([(24, -9), (28, -9 - spout)]), width=LINE * 0.8, amp=0.2, color=WATER)


def xkcd_cloud(ink, cx, cy, w):
    pts = [(cx - w / 2, cy)]
    bumps = [(-0.35, 0.16), (-0.12, 0.22), (0.14, 0.20), (0.36, 0.14)]
    for rel_x, rel_r in bumps:
        b_cx = cx + rel_x * w
        b_r = rel_r * w
        arc = [(b_cx + b_r * math.cos(math.pi + math.pi * i / 10),
                cy - (b_r * 0.8) * math.sin(math.pi * i / 10)) for i in range(11)]
        pts.extend(arc)
    pts.append((cx + w / 2, cy))
    pts.append((cx - w / 2, cy))
    ink.stroke(pts, closed=True, fill=PAPER, width=LINE * 0.8, amp=0.2)


def clouds_2d(ink, phase):
    for k, (cx_base, cy, cw) in enumerate(((770, 44, 82), (885, 38, 62))):
        cx = cx_base + 8.0 * math.sin(2 * math.pi * phase + k * 1.5)
        xkcd_cloud(ink, cx, cy, cw)


def sun_2d(ink, phase, cx=655, cy=42, r=19):
    rays = 12
    spin = 2 * math.pi * phase / rays
    for i in range(rays):
        a = spin + 2 * math.pi * i / rays
        pulse = 1.0 + 0.25 * math.sin(2 * math.pi * phase * 2 + i * 1.1)
        r0 = r + 5
        r1 = r + 5 + 9 * pulse
        ink.stroke([(cx + r0 * math.cos(a), cy + r0 * math.sin(a)),
                    (cx + r1 * math.cos(a), cy + r1 * math.sin(a))], width=LINE * 0.8, amp=0.2)
    ink.stroke(circle(cx, cy, r, 32), fill=SUN, width=LINE * 1.0, amp=0.5)


def dove_2d(ink, phase, dx=600, dy=88):
    flap = 5.0 * math.sin(2 * math.pi * phase * 4)
    sway_x = 8.0 * math.sin(2 * math.pi * phase)
    x = dx + sway_x
    y = dy + flap * 0.3
    ink.stroke([(x, y), (x + 8, y - 2), (x + 4, y - 10 - flap), (x + 10, y - 4)], fill=PAPER, width=LINE * 0.8)
    ink.stroke([(x, y), (x - 6, y + 2), (x - 2, y - 2)], fill=PAPER, width=LINE * 0.7)
    ink.stroke(circle(x + 11, y - 2, 2.5, 10), fill=PAPER, width=LINE * 0.8)
    ink.stroke([(x + 13, y - 2), (x + 17, y - 1)], width=LINE * 0.7)
    bx, by = x + 17, y - 1
    ink.stroke([(bx, by), (bx + 5, by + 4)], width=LINE * 0.7, amp=0.1)
    ink.stroke([(bx + 3, by + 2), (bx + 7, by + 2)], width=LINE * 0.5, amp=0.1)


def speech_bubble_2d(ink, cx, cy, lines, size, tail_to):
    widths = [ink.text_width(s, size) for s in lines]
    max_w = max(widths)
    w = max_w + 32
    h = len(lines) * (size + 5) + 18
    pts = circle(cx, cy, w / 2, 48, ry=h / 2)
    ink.stroke(pts, fill=PAPER, width=LINE * 0.9, amp=0.5)
    bx, by = cx + w * 0.28, cy + h / 2 - 2
    ink.stroke([(bx - 8, by), (tail_to[0], tail_to[1]), (bx + 8, by - 4)], fill=PAPER, width=LINE * 0.9, amp=0.3)
    start_y = cy - (len(lines) * (size + 5)) / 2
    for i, s in enumerate(lines):
        ink.text(cx, start_y + i * (size + 5), s, size, anchor="ma")


def title_2d(ink, ver):
    ink.text(30, 20, "NORDSTJERNEN WEB BROWSER", 36, bold=True)
    ink.text(32, 66, TAGLINE, 18, bold=False)
    ink.text(32, 92, "Version " + ver, 15, bold=False)


def ground_2d(ink):
    ink.stroke([(0, GROUND_Y), (SHORE_X, GROUND_Y)], width=LINE * 1.2, amp=0.3)
    ink.stroke([(SHORE_X, GROUND_Y), (595, GROUND_Y + 18), (610, GROUND_Y + 28)], width=LINE * 1.0, amp=0.4)
    for gx in range(25, 570, 28):
        ink.stroke([(gx, GROUND_Y), (gx - 1, GROUND_Y - 4)], width=LINE * 0.5, amp=0.1)
        ink.stroke([(gx, GROUND_Y), (gx + 2, GROUND_Y - 5)], width=LINE * 0.5, amp=0.1)


def render_frame(i, fonts, ver):
    phase = i / float(FRAMES)
    ink = Ink(fonts)

    ink.start("sun")
    sun_2d(ink, phase)
    ink.start("clouds")
    clouds_2d(ink, phase)
    ink.start("dove")
    dove_2d(ink, phase)
    ink.start("gulls")
    gulls_2d(ink, phase)

    ink.start("ground")
    ground_2d(ink)

    ink.start("water")
    water_2d(ink, phase)
    ink.start("whale")
    whale_2d(ink, phase)

    ink.start("ark")
    ark_2d(ink, phase)

    ink.start("lighthouse")
    lighthouse_2d(ink, phase)

    ink.start("noah")
    noah_2d(ink, phase)
    ink.start("procession")
    procession_2d(ink, phase)

    ink.start("bubble")
    speech_bubble_2d(ink, 420, 134, BUBBLE, 14, (555 - 4, GROUND_Y - 45))
    ink.start("title")
    title_2d(ink, ver)

    return ink.img.resize((W, H), Image.LANCZOS)


def assemble(frames, out_gif):
    sheet = Image.new("RGB", (W, H * len(frames)))
    for i, f in enumerate(frames):
        sheet.paste(f, (0, i * H))
    palette = sheet.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    quantized = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
    quantized[0].save(out_gif, save_all=True, append_images=quantized[1:], loop=0,
                      duration=DELAY_MS, disposal=1, optimize=False)
    if shutil.which("gifsicle"):
        subprocess.run(["gifsicle", "-O3", "--lossy=%d" % LOSSY, "--colors", "256",
                        out_gif, "-o", out_gif], check=True)
    return os.path.getsize(out_gif)


def write_header(gif, header):
    b64 = base64.b64encode(open(gif, "rb").read()).decode()
    lines = textwrap.wrap(b64, 96)
    out = ["/* about_splash_gif.h — the animated 256-colour about:start splash, embedded. */",
           "#ifndef NS_ABOUT_SPLASH_GIF_H", "#define NS_ABOUT_SPLASH_GIF_H", "",
           "static const char about_splash_gif_b64[] ="]
    out += ['    "%s"%s' % (ln, ";" if i == len(lines) - 1 else "")
            for i, ln in enumerate(lines)]
    out += ["", "#endif", ""]
    open(header, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print("wrote %s (%d b64 chars)" % (header, len(b64)))


def main():
    ver = version()
    fonts = (find_font("Comic Neue:bold",
                       "C:/Windows/Fonts/comicbd.ttf",
                       "/usr/share/fonts/opentype/comic-neue/ComicNeue-Bold.otf",
                       "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
                       "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                       "C:/Windows/Fonts/LiberationSans-Bold.ttf",
                       "C:/Windows/Fonts/DejaVuSans-Bold.ttf"),
             find_font("Comic Neue",
                       "C:/Windows/Fonts/comic.ttf",
                       "/usr/share/fonts/opentype/comic-neue/ComicNeue-Regular.otf",
                       "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                       "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                       "C:/Windows/Fonts/LiberationSans-Regular.ttf",
                       "C:/Windows/Fonts/DejaVuSans.ttf"))
    print("rendering %d frames for %s ..." % (FRAMES, ver))
    frames = [render_frame(i, fonts, ver) for i in range(FRAMES)]
    gif = os.path.join(ROOT, "data", "about-splash.gif")
    size = assemble(frames, gif)
    print("assembled %s %df %dx%d (%d bytes)" % (gif, FRAMES, W, H, size))
    write_header(gif, os.path.join(ROOT, "src", "about_splash_gif.h"))
    splash_png = os.path.join(ROOT, "data", "about-splash-%s.png" % ver)
    frames[0].save(splash_png, optimize=True)
    print("wrote %s" % splash_png)


if __name__ == "__main__":
    main()
