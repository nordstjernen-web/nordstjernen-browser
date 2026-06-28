#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ver=$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -n1)
[ -n "$ver" ] || { echo "could not read version from meson.build" >&2; exit 1; }
ver=${ver%%-*}
codename='« Manifest Destiny »'

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
fr=$(find_font 'DejaVu Sans' \
    /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
    /usr/share/fonts/dejavu/DejaVuSans.ttf /usr/share/fonts/TTF/DejaVuSans.ttf)

w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT
W=940; H=320

# summer sky: blue to warm pale at the horizon
convert -size ${W}x${H} gradient:'#4ea7e6'-'#dff3f6' "$w/sky.png"
convert -size ${W}x${H} gradient:none-'#fcf3c6' "$w/warm.png"
convert "$w/sky.png" "$w/warm.png" -compose over -composite "$w/sky1.png"
convert -size ${W}x${H} xc:none -fill '#fff3c8' -draw "ellipse 800,70 250,170 0,360" -blur 0x80 "$w/glow.png"
convert "$w/sky1.png" "$w/glow.png" -compose screen -composite "$w/sky2.png"

# soft clouds
convert "$w/sky2.png" -fill 'rgba(255,255,255,0.85)' \
    -draw "ellipse 250,70 46,16 0,360 ellipse 300,76 40,18 0,360 ellipse 210,80 30,12 0,360" \
    -draw "ellipse 560,52 38,13 0,360 ellipse 600,58 30,12 0,360" \
    -blur 0x2 "$w/sky3.png"

# the sun: warm disc + corona + rays
python3 - > "$w/sun.txt" <<'PY'
import math
cx, cy, R = 800, 72, 40
out = []
for k in range(16):
    a = 2*math.pi*k/16
    r0, r1, wdt = R*1.35, R*2.15, 0.12
    p1 = (cx+r0*math.cos(a-wdt), cy+r0*math.sin(a-wdt))
    p2 = (cx+r1*math.cos(a), cy+r1*math.sin(a))
    p3 = (cx+r0*math.cos(a+wdt), cy+r0*math.sin(a+wdt))
    out.append("fill #ffd84a stroke none polygon %.1f,%.1f %.1f,%.1f %.1f,%.1f" % (p1[0],p1[1],p2[0],p2[1],p3[0],p3[1]))
print(" ".join(out))
PY
rays=$(cat "$w/sun.txt")
convert -size ${W}x${H} xc:none -draw "$rays" -blur 0x1 "$w/rays.png"
convert -size ${W}x${H} xc:none -fill '#ffdf6a' -draw "circle 800,72 800,112" -blur 0x9 "$w/scorona.png"
convert "$w/sky3.png" "$w/rays.png" -compose screen -composite \
        "$w/scorona.png" -compose screen -composite "$w/sky4.png"
convert "$w/sky4.png" -fill '#ffd23e' -draw "circle 800,72 800,108" \
        -fill '#ffe277' -draw "circle 792,64 792,86" "$w/sky5.png"

# rolling green land and a crowd of happy people
scene=$(python3 - "$W" "$H" <<'PY'
import sys, random, math
W, H = int(sys.argv[1]), int(sys.argv[2])
out = []
random.seed(20)

def hx(c): return "#%02x%02x%02x" % (int(c[0]), int(c[1]), int(c[2]))
def dk(c, f=0.8): return tuple(max(0, int(v*f)) for v in c)

def poly(col, pts):
    out.append("fill %s stroke none polygon %s" % (hx(col), " ".join("%.1f,%.1f" % p for p in pts)))
def ell(col, cx, cy, rx, ry):
    out.append("fill %s stroke none ellipse %.1f,%.1f %.1f,%.1f 0,360" % (hx(col), cx, cy, rx, ry))
def quad(col, a, b, wa, wb):
    dx, dy = b[0]-a[0], b[1]-a[1]; L = math.hypot(dx, dy) or 1.0
    nx, ny = -dy/L, dx/L
    poly(col, [(a[0]+nx*wa, a[1]+ny*wa), (b[0]+nx*wb, b[1]+ny*wb),
               (b[0]-nx*wb, b[1]-ny*wb), (a[0]-nx*wa, a[1]-ny*wa)])

def ascale(x):
    t = (x - W*0.40) / (W*0.22)
    return max(0.10, min(1.0, 0.10 + 0.90*t))

def land(base_y, color, amp, seed, drop):
    random.seed(seed)
    def eb(x): return base_y + (1 - ascale(x)) * drop
    pts = [(-30, H), (-30, eb(-30))]
    x = -30
    while x < W + 30:
        y = eb(x) - random.uniform(amp*0.2, amp) * ascale(x)
        pts.append((x, y)); x += 58 + random.uniform(-14, 14)
    pts += [(W+30, eb(W+30)), (W+30, H)]
    out.append("fill %s stroke none polygon %s" % (hx(color), " ".join("%.1f,%.1f" % p for p in pts)))
    return eb

SKIN = [(244,201,166),(232,182,142),(206,156,116),(170,122,86),(142,100,70)]
HAIR = [(38,28,26),(78,52,36),(150,102,52),(212,180,96),(30,30,36),(110,70,44)]
SHIRT = [(228,74,74),(245,170,52),(248,212,72),(92,190,98),(70,172,182),
         (70,120,212),(150,92,196),(232,112,162),(240,132,60),(60,180,150)]
PANTS = [(58,70,96),(92,70,54),(52,92,122),(120,62,72),(70,102,150),(64,64,74)]

def person(cx, fy, h, pose):
    shirt = random.choice(SHIRT); skin = random.choice(SKIN); hair = random.choice(HAIR)
    dress = random.random() < 0.4
    pants = shirt if dress else random.choice(PANTS)
    sh_y = fy - h*0.62; hip_y = fy - h*0.32; head_cy = fy - h*0.84; hr = h*0.15
    if dress:
        poly(shirt, [(cx-h*0.12, sh_y), (cx+h*0.12, sh_y), (cx+h*0.20, fy), (cx-h*0.20, fy)])
    else:
        lw = h*0.075
        poly(pants, [(cx-h*0.11, hip_y), (cx-h*0.11+2*lw, hip_y), (cx-h*0.11+1.6*lw, fy), (cx-h*0.11+0.2*lw, fy)])
        poly(pants, [(cx+h*0.11-2*lw, hip_y), (cx+h*0.11, hip_y), (cx+h*0.11-0.2*lw, fy), (cx+h*0.11-1.6*lw, fy)])
        poly(shirt, [(cx-h*0.14, sh_y), (cx+h*0.14, sh_y), (cx+h*0.12, hip_y+h*0.02), (cx-h*0.12, hip_y+h*0.02)])
    aw = h*0.05
    sL = (cx-h*0.12, sh_y+h*0.02); sR = (cx+h*0.12, sh_y+h*0.02)
    if pose == "up":
        hLp = (cx-h*0.26, sh_y-h*0.40); hRp = (cx+h*0.26, sh_y-h*0.40)
    elif pose == "oneup":
        hLp = (cx-h*0.28, sh_y-h*0.40); hRp = (cx+h*0.16, hip_y)
    else:
        hLp = (cx-h*0.30, sh_y-h*0.04); hRp = (cx+h*0.30, sh_y-h*0.04)
    quad(skin, sL, hLp, aw, aw*0.8); quad(skin, sR, hRp, aw, aw*0.8)
    ell(skin, hLp[0], hLp[1], aw*0.9, aw*0.9); ell(skin, hRp[0], hRp[1], aw*0.9, aw*0.9)
    ell(hair, cx, head_cy-hr*0.18, hr*1.04, hr*1.04)
    ell(skin, cx, head_cy, hr*0.86, hr)

land(H*0.60, (150,200,112), H*0.10, 4, H*0.34)
land(H*0.70, (118,186,92),  H*0.11, 8, H*0.36)
mtop = land(H*0.78, (92,170,74), H*0.09, 15, H*0.40)

random.seed(15)
for _ in range(60):
    fx = random.uniform(W*0.40, W*0.99); fy = random.uniform(mtop(fx)+10, H-6)
    if fy > mtop(fx)+6:
        ell(random.choice([(250,240,90),(252,252,252),(245,140,170)]), fx, fy, 2.4, 2.4)

people = []
rows = [(H*0.80, 34, 7), (H*0.875, 44, 7), (H*0.95, 56, 6)]
for ry, ph, n in rows:
    for i in range(n):
        px = W*0.42 + (W*0.56) * (i + random.uniform(-0.35, 0.35)) / (n-1)
        if px < W*0.40: continue
        pose = random.choices(["up", "oneup", "out"], weights=[5, 3, 2])[0]
        people.append((px, ry + random.uniform(-4, 4), ph * random.uniform(0.9, 1.1), pose))
people.sort(key=lambda p: p[1])
for px, py, ph, pose in people:
    person(px, py, ph, pose)

sys.stdout.write(" ".join(out))
PY
)
convert "$w/sky5.png" -draw "$scene" "$w/scene.png"

# text labels — light weight, slate with a gold accent, over the open left sky
convert -background none -font "$fr" -pointsize 54 -kerning 1 -fill '#2f3d5e' label:'Nordstjernen ' "$w/t1.png"
convert -background none -font "$fr" -pointsize 54 -fill '#d4862a' label:"$ver" "$w/t2.png"
convert -background none -font "$fr" -pointsize 25 -fill '#3f6f8f' label:'Nordstjernen Web Browser' "$w/ts.png"
convert -background none -font "$fr" -pointsize 23 -kerning 3 -fill '#d4862a' label:"$codename" "$w/tc.png"
convert -background none -font "$fr" -pointsize 20 -fill '#5a7088' \
    label:'Étoile du Nord — the legendary web browser' "$w/t3.png"

h1=$(identify -format '%h' "$w/t1.png"); w1=$(identify -format '%w' "$w/t1.png")
hs=$(identify -format '%h' "$w/ts.png"); hc=$(identify -format '%h' "$w/tc.png")
g1=14; g2=14; g3=12
sy=$((46 + h1 + g1)); cy=$((sy + hs + g2)); gy=$((cy + hc + g3)); ty=46; textleft=80

convert "$w/scene.png" \
    "$w/t1.png" -gravity NorthWest -geometry +${textleft}+${ty} -compose over -composite \
    "$w/t2.png" -gravity NorthWest -geometry +$((textleft + w1))+${ty} -compose over -composite \
    "$w/ts.png" -gravity NorthWest -geometry +${textleft}+${sy} -compose over -composite \
    "$w/tc.png" -gravity NorthWest -geometry +${textleft}+${cy} -compose over -composite \
    "$w/t3.png" -gravity NorthWest -geometry +${textleft}+${gy} -compose over -composite \
    "$w/full.png"
convert "$w/full.png" -strip -dither FloydSteinberg -colors 256 -define png:compression-level=9 PNG8:"$w/splash.png"
echo "rendered splash ${W}x${H} for $ver $codename ($(stat -c%s "$w/splash.png") bytes)"

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
