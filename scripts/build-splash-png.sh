#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ver=$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -n1)
[ -n "$ver" ] || { echo "could not read version from meson.build" >&2; exit 1; }
ver=${ver%%-*}
codename='« Firefoxdödaren »'

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
convert -background none -font "$fb" -pointsize 54 -stroke '#000000' -strokewidth 3 -fill '#ffffff' label:'Nordstjernen ' "$w/t1.png"
convert -background none -font "$fb" -pointsize 54 -stroke '#000000' -strokewidth 3 -fill '#ffd27a' label:"$ver" "$w/t2.png"
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

# Frontier: Elite II interceptor — a low-poly mesh banking into a 3/4 ascent,
# flat-shaded with metallic specular + cool rim light and twin glowing engines.
draws=$(python3 - "$w" <<'PY'
import math, sys
wdir = sys.argv[1]
YAW, PITCH, ROLL = math.radians(33), math.radians(48), math.radians(-13)
CW, CH = 720, 400
TARGET_W, TARGET_H = 660, 330
def nrm(a):
    m = math.sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]) or 1.0
    return (a[0]/m, a[1]/m, a[2]/m)
def sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def dot(a, b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
L = nrm((-0.45, 0.66, 0.60)); V = (0.0, 0.0, 1.0)
H = nrm((L[0]+V[0], L[1]+V[1], L[2]+V[2])); SHIN = 26
VERT, FACE = [], []
def add(p): VERT.append(tuple(p)); return len(VERT)-1
PROFILE = [(0.52,0.0),(0.43,0.52),(0.10,0.95),(-0.30,0.66),(-0.42,0.0),
           (-0.30,-0.66),(0.10,-0.95),(0.43,-0.52)]
N = len(PROFILE)
STN = [(86,14,11,0.0),(50,25,15,0.0),(8,32,16,-0.5),(-30,29,14,-1.0),(-50,22,11,-1.5)]
def ring(st):
    x, hz, hy, yc = st
    return [add((x, yc + py*hy, pz*hz)) for (py, pz) in PROFILE]
rings = [ring(s) for s in STN]
nose = add((110, 0.5, 0.0))
def topb(i): return i in (0,1,7)
def botb(i): return i in (3,4,5)
def hullmat(i): return 'deck' if topb(i) else ('belly' if botb(i) else 'flank')
for i in range(N):
    FACE.append(([nose, rings[0][i], rings[0][(i+1)%N]], hullmat(i)))
for s in range(len(rings)-1):
    r0, r1 = rings[s], rings[s+1]
    for i in range(N):
        j = (i+1)%N
        FACE.append(([r0[i], r0[j], r1[j], r1[i]], hullmat(i)))
tailc = add((-52, -1.5, 0.0))
for i in range(N):
    FACE.append(([rings[-1][i], tailc, rings[-1][(i+1)%N]], 'engbay'))
def wing(side):
    rootF = add((46,-1,side*30)); rootB = add((-30,-2,side*27))
    rootFb= add((46,-5,side*30)); rootBb= add((-30,-6,side*27))
    tipF  = add((6,1,side*78));   tipB  = add((-40,-2,side*70))
    tipFb = add((6,-2,side*78));  tipBb = add((-40,-4,side*70))
    FACE.append(([rootF,tipF,tipB,rootB],'wing'))
    FACE.append(([rootFb,rootBb,tipBb,tipFb],'wingb'))
    FACE.append(([rootF,rootFb,tipFb,tipF],'edge'))
    FACE.append(([rootB,tipB,tipBb,rootBb],'edge'))
    FACE.append(([tipF,tipFb,tipBb,tipB],'edge'))
    wl = add((-10,14,side*82))
    FACE.append(([tipF,tipB,wl],'finacc')); FACE.append(([tipFb,wl,tipBb],'finacc'))
wing(1); wing(-1)
cf = add((76,8.4,0)); ca = add((60,16.0,0))
sl = add((52,9.6,8.5)); sr = add((52,9.6,-8.5))
cb1 = add((33,9.8,6)); cb2 = add((33,9.8,-6))
FACE.append(([cf,sl,ca],'glassF')); FACE.append(([cf,ca,sr],'glassF'))
FACE.append(([sl,cb1,ca],'glass')); FACE.append(([sr,ca,cb2],'glass'))
FACE.append(([ca,cb1,cb2],'glass'))
for side in (1,-1):
    a = add((-26,6,side*7)); b = add((-50,6,side*9)); t = add((-44,22,side*16))
    FACE.append(([a,b,t],'finacc')); FACE.append(([b,a,t],'finacc'))
NOZ = []
for side in (1,-1):
    cx, cy, cz = -50, -2, side*9; seg = 8; ri = []
    for k in range(seg):
        ang = 2*math.pi*k/seg
        ri.append(add((cx, cy+5.0*math.sin(ang), cz+5.0*math.cos(ang))))
    ctr = add((cx-3, cy, cz))
    for k in range(seg):
        FACE.append(([ri[k], ctr, ri[(k+1)%seg]], 'glow'))
    NOZ.append((cx-1, cy, cz, 5.2))
BASE = {'deck':(168,176,189),'flank':(118,126,142),'belly':(70,76,92),
        'wing':(140,149,165),'wingb':(86,92,108),'edge':(150,160,176),
        'engbay':(48,52,62),'glass':(24,38,64),'glassF':(96,156,200),
        'finacc':(64,150,196)}
GLOW = (150, 238, 255)
def rot(p):
    x, y, z = p
    x, z = x*math.cos(YAW)+z*math.sin(YAW), -x*math.sin(YAW)+z*math.cos(YAW)
    y, z = y*math.cos(PITCH)-z*math.sin(PITCH), y*math.sin(PITCH)+z*math.cos(PITCH)
    x, y = x*math.cos(ROLL)-y*math.sin(ROLL), x*math.sin(ROLL)+y*math.cos(ROLL)
    return (x, y, z)
RV = [rot(p) for p in VERT]
faces = []
for idx, mat in FACE:
    pr = [RV[i] for i in idx]
    n = nrm(cross(sub(pr[1], pr[0]), sub(pr[2], pr[0])))
    if n[2] < 0: n = (-n[0], -n[1], -n[2])
    depth = sum(p[2] for p in pr) / len(pr)
    if mat == 'glow':
        col = GLOW
    else:
        diff = max(0.0, dot(n, L)); spec = max(0.0, dot(n, H))**SHIN
        ks = 0.85 if mat in ('glassF','glass','edge') else 0.5
        br, bg, bb = BASE[mat]; rim = (1.0-max(0.0, dot(n, V)))**3 * 0.55
        r = br*(0.34+0.82*diff)+255*ks*spec+90*rim
        g = bg*(0.34+0.82*diff)+255*ks*spec+150*rim
        b = bb*(0.34+0.82*diff)+255*ks*spec+210*rim
        col = (min(255,int(r)), min(255,int(g)), min(255,int(b)))
    faces.append((depth, col, [(p[0], -p[1]) for p in pr]))
allp = [p for _, _, ps in faces for p in ps]
xs = [p[0] for p in allp]; ys = [p[1] for p in allp]
minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
sc = min(TARGET_W/(maxx-minx), TARGET_H/(maxy-miny))
ox = (CW-(maxx-minx)*sc)/2-minx*sc; oy = (CH-(maxy-miny)*sc)/2-miny*sc
faces.sort(key=lambda f: f[0])
draws = []
for _, col, ps in faces:
    pts = " ".join("%.1f,%.1f" % (p[0]*sc+ox, p[1]*sc+oy) for p in ps)
    draws.append("fill #%02x%02x%02x polygon %s" % (col[0], col[1], col[2], pts))
aft = rot((-1.0, 0.0, 0.0)); tx, ty = aft[0], -aft[1]
tl = math.hypot(tx, ty) or 1.0; ux, uy = tx/tl, ty/tl
def lerp(a, b, t): return a+(b-a)*t
def plasma(t):
    stops = [(0.0,(234,251,255)),(0.28,(150,238,255)),(0.62,(70,168,255)),(1.0,(126,104,255))]
    for i in range(len(stops)-1):
        t0, c0 = stops[i]; t1, c1 = stops[i+1]
        if t <= t1:
            f = (t-t0)/(t1-t0) if t1 > t0 else 0.0
            return tuple(int(lerp(c0[k], c1[k], f)) for k in range(3))
    return stops[-1][1]
nz, plume, pcore = [], [], []
for (x, y, z, r) in NOZ:
    rp = rot((x, y, z)); sx = rp[0]*sc+ox; sy = (-rp[1])*sc+oy; rr = r*sc
    nz.append((sx, sy, rr))
    Lo = rr*4.3; nseg = 14
    for k in range(nseg):
        t = k/(nseg-1)
        plume.append((sx+ux*Lo*t, sy+uy*Lo*t, rr*(1.3*(1-t)+0.14), plasma(t)))
    Lc = rr*2.7; ncore = 10
    for k in range(ncore):
        t = k/(ncore-1)
        pcore.append((sx+ux*Lc*t, sy+uy*Lc*t, rr*(0.72*(1-t)+0.08)))
with open(wdir+"/nozzles.txt", "w") as f:
    for (sx, sy, rr) in nz: f.write("%.1f %.1f %.1f\n" % (sx, sy, rr))
with open(wdir+"/plume.txt", "w") as f:
    for (px, py, rad, c) in plume: f.write("%.1f %.1f %.1f %d %d %d\n" % (px, py, rad, c[0], c[1], c[2]))
with open(wdir+"/pcore.txt", "w") as f:
    for (px, py, rad) in pcore: f.write("%.1f %.1f %.1f\n" % (px, py, rad))
sys.stdout.write(" ".join(draws))
PY
)
convert -size 720x400 xc:none -stroke '#0b1119' -strokewidth 0.8 -draw "$draws" "$w/hull.png"
gd=$(awk '{printf "fill rgba(120,232,255,1) circle %s,%s %s,%s ",$1,$2,$1+$3,$2}' "$w/nozzles.txt")
cd=$(awk '{r=$3*0.45; printf "fill rgba(232,252,255,1) circle %s,%s %s,%s ",$1,$2,$1+r,$2}' "$w/nozzles.txt")
pd=$(awk '{printf "fill #%02x%02x%02x circle %s,%s %s,%s ",$4,$5,$6,$1,$2,$1+$3,$2}' "$w/plume.txt")
pc=$(awk '{printf "fill rgba(236,253,255,1) circle %s,%s %s,%s ",$1,$2,$1+$3,$2}' "$w/pcore.txt")
convert -size 720x400 xc:none -draw "$gd" -blur 0x6 -channel A -evaluate multiply 1.3 +channel "$w/glow.png"
convert -size 720x400 xc:none -draw "$cd" -blur 0x2 "$w/core.png"
convert -size 720x400 xc:none -draw "$pd" -blur 0x5 -channel A -evaluate multiply 1.25 +channel "$w/plume.png"
convert -size 720x400 xc:none -draw "$pc" -blur 0x3 "$w/pcore.png"
convert "$w/hull.png" \
        "$w/glow.png" -compose screen -composite \
        "$w/plume.png" -compose screen -composite \
        "$w/core.png" -compose screen -composite \
        "$w/pcore.png" -compose screen -composite \
        -trim +repage -resize 232x "$w/ship.png"

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
    \( "$w/ship.png" \) -gravity NorthEast -geometry +14+8 -compose over -composite \
    "$w/t1.png" -gravity NorthWest -geometry +${textleft}+${ty} -compose over -composite \
    "$w/t2.png" -gravity NorthWest -geometry +$((textleft + w1))+${ty} -compose over -composite \
    "$w/ts.png" -gravity NorthWest -geometry +${textleft}+${sy} -compose over -composite \
    "$w/tc.png" -gravity NorthWest -geometry +${textleft}+${cy} -compose over -composite \
    "$w/t3.png" -gravity NorthWest -geometry +${textleft}+${gy} -compose over -composite \
    "$w/full.png"
convert "$w/full.png" -strip -dither FloydSteinberg -colors 256 -define png:compression-level=9 PNG8:"$w/splash.png"
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
