#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ver=$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -n1)
[ -n "$ver" ] || { echo "could not read version from meson.build" >&2; exit 1; }
codename='« Polaris »'

find_font() {
    local q=$1; shift
    if command -v fc-match >/dev/null 2>&1; then
        local f; f=$(fc-match -f '%{file}' "$q" 2>/dev/null || true)
        [ -n "$f" ] && [ -f "$f" ] && { echo "$f"; return 0; }
    fi
    local p
    for p in "$@"; do [ -f "$p" ] && { echo "$p"; return 0; }; done
    echo "missing font: $q" >&2; return 1
}
fb=$(find_font 'DejaVu Sans:bold' \
    /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf \
    /usr/share/fonts/dejavu/DejaVuSans-Bold.ttf /usr/share/fonts/TTF/DejaVuSans-Bold.ttf)
fr=$(find_font 'DejaVu Sans' \
    /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
    /usr/share/fonts/dejavu/DejaVuSans.ttf /usr/share/fonts/TTF/DejaVuSans.ttf)

w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT

# text labels
convert -background none -font "$fb" -pointsize 54 -fill '#ffffff' label:'Nordstjernen ' "$w/t1.png"
convert -background none -font "$fb" -pointsize 54 -fill '#7fb0ff' label:"$ver" "$w/t2.png"
convert -background none -font "$fb" -pointsize 27 -fill '#e6edff' label:'Nordstjernen Web Browser' "$w/ts.png"
convert -background none -font "$fb" -pointsize 25 -kerning 3 -fill '#ffd27a' label:"$codename" "$w/tc.png"
convert -background none -font "$fr" -pointsize 21 -fill '#cdd8ee' \
    label:'Étoile du Nord — the legendary web browser' "$w/t3.png"
w1=$(identify -format '%w' "$w/t1.png"); h1=$(identify -format '%h' "$w/t1.png")
w2=$(identify -format '%w' "$w/t2.png")
hs=$(identify -format '%h' "$w/ts.png"); hc=$(identify -format '%h' "$w/tc.png"); h3=$(identify -format '%h' "$w/t3.png")
textleft=210; rm=70
titlew=$((textleft + w1 + w2)); cw=$((titlew + rm)); [ "$cw" -lt 940 ] && cw=940
ty=84; sy=$((ty + h1 + 12)); cy=$((sy + hs + 12)); gy=$((cy + hc + 10)); ch=$((gy + h3 + 26))

# night sky
convert -size ${cw}x${ch} gradient:'#0a1430'-'#1b2c66' "$w/sky.png"

# real aurora
ha=200
convert -size ${cw}x1 xc: +noise Random -colorspace Gray -scale ${cw}x${ha}\! -level 42%,100% -gamma 1.5 "$w/rays.png"
convert -size ${cw}x${ha} gradient:black-'#8a8a8a' "$w/vfade.png"
convert "$w/rays.png" "$w/vfade.png" -compose multiply -composite "$w/curtainA.png"
convert -size ${cw}x${ha} gradient:'#5fe0ff'-'#27ff86' "$w/auroCol.png"
convert "$w/auroCol.png" "$w/curtainA.png" -alpha off -compose CopyOpacity -composite "$w/auroM.png"
convert -size ${cw}x${ha} gradient:'#b060ff'-none "$w/mag.png"
convert "$w/auroM.png" \( "$w/mag.png" -evaluate multiply 0.26 \) -compose screen -composite "$w/auroM2.png"
convert "$w/auroM2.png" -background none -wave 7x300 -blur 0x2 "$w/aurora.png"

# north-star sparkle
convert -size 200x200 xc:none -fill white -draw "circle 100,100 100,108" -blur 0x7 "$w/glow.png"
convert -size 200x200 xc:none -stroke white -fill white \
    -strokewidth 2 -draw "line 100,25 100,175" -strokewidth 2 -draw "line 25,100 175,100" \
    -strokewidth 1 -draw "line 55,55 145,145" -strokewidth 1 -draw "line 145,55 55,145" -blur 0x0.6 "$w/spikes.png"
convert "$w/glow.png" "$w/spikes.png" -compose screen -composite -resize 140x140 "$w/star.png"

# Frontier: Elite II ship — a low-poly mesh rotated to a banking 3/4 view and flat-shaded.
draws=$(python3 - <<'PY'
import math, sys
YAW, PITCH, ROLL = math.radians(-34), math.radians(56), math.radians(20)
CW, CH = 480, 240
TARGET_W, TARGET_H = 446, 210
L = (-0.35, 0.58, 0.74)
V, F = [], []
def add(v): V.append(v); return len(V) - 1
def hexring(x, r, sq=0.60):
    idx = []
    for ang in (90, 30, 330, 270, 210, 150):
        a = math.radians(ang)
        idx.append(add((x, r * math.sin(a) * sq, r * math.cos(a))))
    return idx
nose = add((98, 0, 0))
rA = hexring(60, 9); rB = hexring(8, 21); rC = hexring(-34, 17); rD = hexring(-47, 13)
for i in range(6):
    F.append(([nose, rA[i], rA[(i + 1) % 6]], 'hull'))
for r0, r1 in ((rA, rB), (rB, rC), (rC, rD)):
    for i in range(6):
        F.append(([r0[i], r0[(i + 1) % 6], r1[(i + 1) % 6], r1[i]], 'hull'))
F.append((list(reversed(rD)), 'eng'))
def wing(s):
    rf = add((16, -3, s * 13)); rb = add((-30, -3, s * 12))
    tu = add((-14, 0, s * 54)); tl = add((-14, -6, s * 54))
    rfl = add((16, -7, s * 13)); rbl = add((-30, -7, s * 12))
    F.append(([rf, tu, rb], 'wing')); F.append(([rfl, rbl, tl], 'wing'))
    F.append(([rf, rfl, tl, tu], 'wing')); F.append(([rb, tu, tl, rbl], 'wing'))
wing(-1); wing(1)
c1 = add((52, 11, 5)); c2 = add((52, 11, -5)); c3 = add((28, 14, 4)); c4 = add((28, 14, -4))
F.append(([c1, c2, c4, c3], 'cock'))
f1 = add((-20, 8, 0)); f2 = add((-44, 8, 0)); f3 = add((-30, 26, 0))
F.append(([f1, f2, f3], 'fin'))
BASE = {'hull': (118, 130, 66), 'wing': (98, 110, 56), 'eng': (96, 100, 86),
        'cock': (30, 40, 44), 'fin': (86, 98, 50)}
def rot(p):
    x, y, z = p
    x, z = x * math.cos(YAW) + z * math.sin(YAW), -x * math.sin(YAW) + z * math.cos(YAW)
    y, z = y * math.cos(PITCH) - z * math.sin(PITCH), y * math.sin(PITCH) + z * math.cos(PITCH)
    x, y = x * math.cos(ROLL) - y * math.sin(ROLL), x * math.sin(ROLL) + y * math.cos(ROLL)
    return (x, y, z)
RV = [rot(p) for p in V]
def sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def nrm(a):
    m = math.sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]) or 1.0
    return (a[0]/m, a[1]/m, a[2]/m)
Ln = nrm(L)
faces = []
for idx, mat in F:
    pr = [RV[i] for i in idx]
    n = nrm(cross(sub(pr[1], pr[0]), sub(pr[2], pr[0])))
    if n[2] < 0: n = (-n[0], -n[1], -n[2])
    bright = 0.34 + 0.78 * max(0.0, n[0]*Ln[0] + n[1]*Ln[1] + n[2]*Ln[2])
    br, bg, bb = BASE[mat]
    col = (min(255, int(br*bright)), min(255, int(bg*bright)), min(255, int(bb*bright)))
    depth = sum(p[2] for p in pr) / len(pr)
    faces.append((depth, col, [(p[0], -p[1]) for p in pr]))
xs = [p[0] for _, _, ps in faces for p in ps]; ys = [p[1] for _, _, ps in faces for p in ps]
minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
sc = min(TARGET_W/(maxx-minx), TARGET_H/(maxy-miny))
ox = (CW - (maxx-minx)*sc)/2 - minx*sc; oy = (CH - (maxy-miny)*sc)/2 - miny*sc
faces.sort(key=lambda f: f[0])
out = []
for _, col, ps in faces:
    pts = " ".join("%.1f,%.1f" % (p[0]*sc+ox, p[1]*sc+oy) for p in ps)
    out.append("fill #%02x%02x%02x polygon %s" % (col[0], col[1], col[2], pts))
sys.stdout.write(" ".join(out))
PY
)
convert -size 480x240 xc:none -stroke '#1c2110' -strokewidth 0.8 -draw "$draws" -trim +repage -resize 212x "$w/ship.png"

# compose
convert "$w/sky.png" \( "$w/aurora.png" -channel A -evaluate multiply 0.82 +channel -gravity North -geometry +50+14 \) -compose screen -composite "$w/sky2.png"
stars=$(python3 - "$cw" "$ch" <<'PY'
import sys, random
W, H = int(sys.argv[1]), int(sys.argv[2])
random.seed(11)
o = []
for _ in range(150):
    x = random.randint(0, W); y = random.randint(0, H - 30)
    r = random.choice([0.4, 0.6, 0.6, 0.8, 1.0, 1.3]); op = round(random.uniform(0.25, 0.92), 2)
    o.append("fill rgba(255,255,255,%s) circle %d,%d %.1f,%d" % (op, x, y, x + r, y))
print(" ".join(o))
PY
)
convert "$w/sky2.png" -draw "$stars" "$w/sky3.png"
convert "$w/sky3.png" \
    \( "$w/star.png" \) -gravity NorthWest -geometry +35+76 -compose screen -composite \
    \( "$w/ship.png" \) -gravity NorthEast -geometry +16+8 -compose over -composite \
    "$w/t1.png" -gravity NorthWest -geometry +${textleft}+${ty} -compose over -composite \
    "$w/t2.png" -gravity NorthWest -geometry +$((textleft + w1))+${ty} -compose over -composite \
    "$w/ts.png" -gravity NorthWest -geometry +${textleft}+${sy} -compose over -composite \
    "$w/tc.png" -gravity NorthWest -geometry +${textleft}+${cy} -compose over -composite \
    "$w/t3.png" -gravity NorthWest -geometry +${textleft}+${gy} -compose over -composite \
    "$w/full.png"
convert "$w/full.png" -strip -dither FloydSteinberg -colors 220 -define png:compression-level=9 PNG8:"$w/splash.png"
echo "rendered splash ${cw}x${ch} for $ver $codename ($(stat -c%s "$w/splash.png") bytes)"

header="src/about_splash_png.h"
python3 - "$w/splash.png" "$header" <<'PY'
import base64, sys, textwrap
png, header = sys.argv[1], sys.argv[2]
b64 = base64.b64encode(open(png, "rb").read()).decode()
lines = textwrap.wrap(b64, 96)
out = ["/* about_splash_png.h — the about:start release splash image, embedded. */",
       "#ifndef NS_ABOUT_SPLASH_PNG_H", "#define NS_ABOUT_SPLASH_PNG_H", "",
       "static const char about_splash_png_b64[] ="]
out += ['    "%s"%s' % (ln, ";" if i == len(lines) - 1 else "")
        for i, ln in enumerate(lines)]
out += ["", "#endif", ""]
open(header, "w", newline="\n").write("\n".join(out))
print("wrote %s (%d b64 chars)" % (header, len(b64)))
PY
