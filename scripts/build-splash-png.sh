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

# two women under the aurora and the North Star — flat-shaded vector figures.
wfig=$(python3 - <<'PY'
import sys, math

W, H = 560, 820

def hexs(c): return "#%02x%02x%02x" % (int(c[0]), int(c[1]), int(c[2]))
def dk(c, f=0.80): return tuple(max(0, int(v*f)) for v in c)
def lt(c, f=1.16): return tuple(min(255, int(v*f)) for v in c)

OUT = []
SHADOW = []

def poly(col, pts, stroke=None, sw=1.0, alpha=None):
    s = ""
    s += ("stroke %s " % hexs(stroke)) if stroke else "stroke none "
    if stroke: s += "stroke-width %.2f " % sw
    fill = ("rgba(%d,%d,%d,%.2f)" % (int(col[0]), int(col[1]), int(col[2]), alpha)) if alpha is not None else hexs(col)
    s += "fill %s polygon %s" % (fill, " ".join("%.1f,%.1f" % p for p in pts))
    OUT.append(s)

def ell(col, cx, cy, rx, ry, alpha=None):
    fill = ("rgba(%d,%d,%d,%.2f)" % (int(col[0]), int(col[1]), int(col[2]), alpha)) if alpha is not None else hexs(col)
    OUT.append("stroke none fill %s ellipse %.1f,%.1f %.1f,%.1f 0,360" % (fill, cx, cy, rx, ry))

def smile(cx, cy, w, col):
    OUT.append("stroke %s stroke-width 2.0 fill none path 'M %.1f,%.1f Q %.1f,%.1f %.1f,%.1f'"
               % (hexs(col), cx-w, cy, cx, cy+w*0.9, cx+w, cy))

def woman(cx, fy, h, skin, hair, dress, style, lean=0.0):
    skin_sh = dk(skin, 0.88); dress_sh = dk(dress, 0.78); dress_hi = lt(dress, 1.12)
    hair_hi = lt(hair, 1.4)
    top = fy - h
    hrx, hry = h*0.052, h*0.064
    hcx = cx + lean*h*0.05
    hcy = top + hry + h*0.012
    neck_w = h*0.026
    sh_y = hcy + hry + h*0.060
    sh_w = h*0.108
    waist_y = sh_y + h*0.180
    waist_w = h*0.064
    hem_y = fy - h*0.045
    hem_w = h*0.150

    # ground shadow
    SHADOW.append("fill rgba(6,8,18,0.45) ellipse %.1f,%.1f %.1f,%.1f 0,360" % (cx, fy+h*0.006, hem_w*1.05, h*0.022))

    # back hair (long styles drape behind shoulders)
    if style in ("long", "wavy"):
        poly(dk(hair, 0.92), [
            (hcx-hrx*0.9, hcy-hry*0.2), (hcx-hrx*1.25, sh_y+h*0.02),
            (hcx-hrx*0.95, waist_y), (hcx-hrx*0.2, waist_y+h*0.02),
            (hcx+hrx*0.2, waist_y+h*0.02), (hcx+hrx*0.95, waist_y),
            (hcx+hrx*1.25, sh_y+h*0.02), (hcx+hrx*0.9, hcy-hry*0.2)])

    # gown
    gown = [
        (cx-sh_w, sh_y), (cx-waist_w, waist_y),
        (cx-hem_w, hem_y-h*0.02), (cx-hem_w*0.95, hem_y), (cx, hem_y+h*0.014),
        (cx+hem_w*0.95, hem_y), (cx+hem_w, hem_y-h*0.02),
        (cx+waist_w, waist_y), (cx+sh_w, sh_y),
        (cx+sh_w*0.46, sh_y-h*0.012), (cx, sh_y+h*0.022), (cx-sh_w*0.46, sh_y-h*0.012)]
    poly(dress, gown)
    # shaded right half (flat-shaded form)
    poly(dress_sh, [(cx, sh_y+h*0.022), (cx, hem_y+h*0.014), (cx+hem_w*0.95, hem_y),
                    (cx+hem_w, hem_y-h*0.02), (cx+waist_w, waist_y),
                    (cx+sh_w, sh_y), (cx+sh_w*0.46, sh_y-h*0.012)], alpha=0.55)
    # highlight seam on the lit side
    poly(dress_hi, [(cx-sh_w*0.2, sh_y+h*0.03), (cx-waist_w*0.55, waist_y),
                    (cx-hem_w*0.34, hem_y-h*0.01), (cx-hem_w*0.16, hem_y-h*0.01),
                    (cx-waist_w*0.2, waist_y), (cx-sh_w*0.02, sh_y+h*0.03)], alpha=0.35)

    # shoes peeking
    for sgn in (-1, 1):
        sxx = cx + sgn*h*0.045
        poly((44, 40, 52), [(sxx-h*0.028, hem_y+h*0.012), (sxx+h*0.030, hem_y+h*0.012),
                            (sxx+h*0.034, fy), (sxx-h*0.024, fy)])

    # arms
    aw = h*0.026
    # left arm down
    poly(skin, [(cx-sh_w*0.9, sh_y+h*0.01), (cx-sh_w*0.62, sh_y+h*0.01),
                (cx-waist_w*1.1, waist_y+h*0.03), (cx-waist_w*1.5, waist_y+h*0.03)])
    # right arm down
    poly(skin, [(cx+sh_w*0.62, sh_y+h*0.01), (cx+sh_w*0.9, sh_y+h*0.01),
                (cx+waist_w*1.5, waist_y+h*0.03), (cx+waist_w*1.1, waist_y+h*0.03)])
    # hands
    ell(skin, cx-waist_w*1.3, waist_y+h*0.045, aw*0.7, aw*0.8)
    ell(skin, cx+waist_w*1.3, waist_y+h*0.045, aw*0.7, aw*0.8)

    # neck
    poly(skin_sh, [(hcx-neck_w, hcy+hry*0.55), (hcx+neck_w, hcy+hry*0.55),
                   (hcx+neck_w*0.9, sh_y+h*0.004), (hcx-neck_w*0.9, sh_y+h*0.004)])

    # head
    ell(skin, hcx, hcy, hrx, hry)

    # front hair / framing
    if style == "bun":
        ell(hair, hcx, hcy-hry*0.72, hrx*0.55, hry*0.42)            # top bun
        poly(hair, [(hcx-hrx*1.04, hcy+hry*0.1), (hcx-hrx*1.04, hcy-hry*0.55),
                    (hcx-hrx*0.4, hcy-hry*1.16), (hcx+hrx*0.4, hcy-hry*1.16),
                    (hcx+hrx*1.04, hcy-hry*0.55), (hcx+hrx*1.04, hcy+hry*0.1),
                    (hcx+hrx*0.78, hcy-hry*0.1), (hcx+hrx*0.6, hcy-hry*0.5),
                    (hcx-hrx*0.6, hcy-hry*0.5), (hcx-hrx*0.78, hcy-hry*0.1)])
    else:
        poly(hair, [(hcx-hrx*1.12, hcy+hry*0.5), (hcx-hrx*1.16, hcy-hry*0.5),
                    (hcx-hrx*0.5, hcy-hry*1.18), (hcx+hrx*0.5, hcy-hry*1.18),
                    (hcx+hrx*1.16, hcy-hry*0.5), (hcx+hrx*1.12, hcy+hry*0.5),
                    (hcx+hrx*0.82, hcy-hry*0.05), (hcx+hrx*0.62, hcy-hry*0.55),
                    (hcx+hrx*0.2, hcy-hry*0.86), (hcx-hrx*0.2, hcy-hry*0.86),
                    (hcx-hrx*0.62, hcy-hry*0.55), (hcx-hrx*0.82, hcy-hry*0.05)])
    # hair highlight
    OUT.append("stroke %s stroke-width 2.2 fill none path 'M %.1f,%.1f Q %.1f,%.1f %.1f,%.1f'"
               % (hexs(hair_hi), hcx-hrx*0.7, hcy-hry*0.5, hcx-hrx*0.95, hcy*1.0, hcx-hrx*0.78, hcy+hry*0.3))

    # face
    eye_y = hcy + hry*0.02
    ell((40, 32, 36), hcx-hrx*0.34, eye_y, hrx*0.085, hry*0.10)
    ell((40, 32, 36), hcx+hrx*0.34, eye_y, hrx*0.085, hry*0.10)
    ell((228, 142, 138), hcx-hrx*0.55, eye_y+hry*0.30, hrx*0.16, hry*0.10, alpha=0.5)
    ell((228, 142, 138), hcx+hrx*0.55, eye_y+hry*0.30, hrx*0.16, hry*0.10, alpha=0.5)
    smile(hcx, eye_y+hry*0.42, hrx*0.26, dk(skin, 0.6))


# woman B (right, slightly behind) drawn first
woman(338, 720, 540, (170, 120, 86), (28, 24, 26), (58, 166, 156), "bun", lean=0.10)
# woman A (left, front)
woman(212, 726, 566, (236, 190, 158), (72, 46, 38), (220, 84, 96), "wavy", lean=-0.06)

sys.stdout.write(" ".join(OUT))

PY
)
convert -size 560x820 xc:none -draw "$wfig" -trim +repage -resize x252 "$w/women.png"

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
    \( "$w/women.png" \) -gravity NorthEast -geometry +40+18 -compose over -composite \
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
