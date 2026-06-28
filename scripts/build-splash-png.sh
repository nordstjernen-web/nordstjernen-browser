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

# colourful hot air balloon — a surface-of-revolution envelope split into
# bright flat-shaded gores, riding a wicker basket lit by a burner flame.
bw=460; bh=720
bdraw=$(python3 - "$w" <<'PY'
import math, sys
wdir = sys.argv[1]
cx = 230.0
apex_y = 26.0
H = 470.0
throat_y = apex_y + H
R = 196.0
CTRL = [(0.00,0.05),(0.05,0.33),(0.12,0.60),(0.22,0.84),(0.34,0.97),
        (0.46,1.00),(0.58,0.965),(0.70,0.85),(0.80,0.67),(0.88,0.49),
        (0.94,0.35),(1.00,0.235)]
def width(u):
    if u <= 0.0: return CTRL[0][1]*R
    if u >= 1.0: return CTRL[-1][1]*R
    for i in range(len(CTRL)-1):
        u0, w0 = CTRL[i]; u1, w1 = CTRL[i+1]
        if u <= u1:
            t = (u-u0)/(u1-u0); t = t*t*(3-2*t)
            return (w0+(w1-w0)*t)*R
    return CTRL[-1][1]*R
N = 16
STEPS = 64
PAL = [(229,55,55),(240,131,38),(247,201,58),(86,192,98),
       (38,180,178),(54,116,214),(150,86,200),(228,94,160)]
phiL = math.radians(-34)
def sh(c, f): return tuple(min(255, max(0, int(v*f))) for v in c)
out = []
half = math.pi/N
for i in range(N):
    pl = -math.pi/2 + i*half; pr = pl + half; pc = (pl+pr)/2
    diff = max(0.0, math.cos(pc - phiL))
    f = 0.45 + 0.63*diff
    idx = i if i < N//2 else N-1-i
    col = sh(PAL[idx % len(PAL)], f)
    pts = []
    for s in range(STEPS+1):
        u = s/STEPS; pts.append((cx + width(u)*math.sin(pl), apex_y + u*H))
    for s in range(STEPS, -1, -1):
        u = s/STEPS; pts.append((cx + width(u)*math.sin(pr), apex_y + u*H))
    poly = " ".join("%.1f,%.1f" % p for p in pts)
    out.append("stroke rgba(18,14,24,0.42) stroke-width 1.0 fill #%02x%02x%02x polygon %s" % (col[0], col[1], col[2], poly))
out.append("stroke none fill rgba(245,245,250,0.16) ellipse %.1f,%.1f %.1f,%.1f 0,360" % (cx, apex_y+10, 24, 11))
out.append("stroke rgba(18,14,24,0.30) stroke-width 1.0 fill none ellipse %.1f,%.1f %.1f,%.1f 0,360" % (cx, apex_y+10, 24, 11))
tw = width(1.0)
out.append("stroke none fill rgba(24,18,26,0.90) ellipse %.1f,%.1f %.1f,%.1f 0,360" % (cx, throat_y, tw, 12))
out.append("stroke none fill rgba(58,44,36,1) ellipse %.1f,%.1f %.1f,%.1f 0,360" % (cx, throat_y, tw*0.6, 6))
btop = throat_y + 96
bbot = btop + 72
btw, bbw = 47.0, 41.0
for a in (-74, -46, -20, 20, 46, 74):
    rx = cx + tw*math.sin(math.radians(a))
    side = 1 if a > 0 else -1
    bx = cx + side*btw
    midx = cx + 0.35*(bx - cx)
    out.append("stroke rgba(222,226,234,0.65) stroke-width 1.0 fill none line %.1f,%.1f %.1f,%.1f" % (rx, throat_y+5, midx, throat_y+34))
    out.append("stroke rgba(222,226,234,0.65) stroke-width 1.0 fill none line %.1f,%.1f %.1f,%.1f" % (midx, throat_y+34, bx, btop))
out.append("stroke none fill rgba(36,30,26,1) ellipse %.1f,%.1f %.1f,%.1f 0,360" % (cx, throat_y+34, 10, 4))
out.append("stroke rgba(70,44,24,0.9) stroke-width 1.2 fill #b07c46 polygon %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f" % (cx-btw, btop, cx+btw, btop, cx+bbw, bbot, cx-bbw, bbot))
out.append("stroke none fill #6e4a28 roundrectangle %.1f,%.1f %.1f,%.1f 4,4" % (cx-btw-3, btop-6, cx+btw+3, btop+7))
nv = 7
for k in range(1, nv):
    t = k/nv
    xt = (cx-btw) + (2*btw)*t
    xb = (cx-bbw) + (2*bbw)*t
    out.append("stroke rgba(94,62,34,0.6) stroke-width 1.0 fill none line %.1f,%.1f %.1f,%.1f" % (xt, btop+8, xb, bbot-2))
for k in range(1, 3):
    yy = btop + (bbot-btop)*k/3.0
    wl = btw + (bbw-btw)*(k/3.0)
    out.append("stroke rgba(94,62,34,0.55) stroke-width 1.0 fill none line %.1f,%.1f %.1f,%.1f" % (cx-wl, yy, cx+wl, yy))
fy = throat_y + 4
flame_big = "fill #ff7a1e polygon %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f" % (
    cx, fy, cx+9, fy+24, cx+5, fy+44, cx, fy+38, cx-5, fy+44, cx-9, fy+24)
flame_core = "fill #ffe27a polygon %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f" % (cx, fy+8, cx+4, fy+28, cx, fy+38, cx-4, fy+28)
open(wdir+"/flame.txt", "w").write(flame_big + " " + flame_core)
open(wdir+"/flameglow.txt", "w").write("fill rgba(255,150,40,0.85) ellipse %.1f,%.1f %.1f,%.1f 0,360" % (cx, fy+24, 16, 28))
open(wdir+"/sheen.txt", "w").write("fill rgba(255,255,255,0.5) ellipse %.1f,%.1f %.1f,%.1f 0,360" % (cx-58, apex_y+150, 58, 150))
sys.stdout.write(" ".join(out))
PY
)
flame=$(cat "$w/flame.txt"); flameglow=$(cat "$w/flameglow.txt"); sheen=$(cat "$w/sheen.txt")
convert -size ${bw}x${bh} xc:none -draw "$bdraw" "$w/bbody.png"
convert -size ${bw}x${bh} xc:none -draw "$sheen" -blur 0x24 "$w/bsheen.png"
convert "$w/bsheen.png" "$w/bbody.png" -alpha on -compose DstIn -composite "$w/bsheenc.png"
convert -size ${bw}x${bh} xc:none -draw "$flameglow" -blur 0x12 "$w/bfglow.png"
convert -size ${bw}x${bh} xc:none -draw "$flame" -blur 0x2 "$w/bflame.png"
convert "$w/bbody.png" \
        "$w/bsheenc.png" -compose screen -composite \
        "$w/bfglow.png" -compose screen -composite \
        "$w/bflame.png" -compose screen -composite \
        -trim +repage -resize x242 "$w/balloon.png"

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
    \( "$w/balloon.png" \) -gravity NorthEast -geometry +26+12 -compose over -composite \
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
