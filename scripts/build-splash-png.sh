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

# dawn sky: lavender to apricot, warmed near the horizon and around the star
convert -size ${W}x${H} gradient:'#8a85bf'-'#f3bfa2' "$w/sky.png"
convert -size ${W}x${H} gradient:none-'#f8ddc2' "$w/warm.png"
convert "$w/sky.png" "$w/warm.png" -compose over -composite "$w/sky1.png"
convert -size ${W}x${H} xc:none -fill '#ffe9cf' -draw "ellipse 792,86 240,150 0,360" -blur 0x70 "$w/glow.png"
convert "$w/sky1.png" "$w/glow.png" -compose screen -composite "$w/sky2.png"

# low-poly mountains sloping into the right, with two figures on the ridge
scene=$(python3 - "$W" "$H" <<'PY'
import sys, random, math
W, H = int(sys.argv[1]), int(sys.argv[2])
out = []

def ridge(base_y, color, amp, seed):
    random.seed(seed)
    def ascale(x):
        t = (x - W*0.56) / (W*0.20)
        return max(0.12, min(1.0, 0.12 + 0.88*t))
    def ebase(x):
        return base_y + (1 - ascale(x)) * (H*0.42)
    pts = [(-30, H), (-30, ebase(-30))]
    x = -30
    hi = True
    while x < W + 30:
        a = ascale(x)
        y = ebase(x) - (random.uniform(amp*0.55, amp) if hi else random.uniform(0, amp*0.22)) * a
        hi = not hi
        pts.append((x, y))
        x += 66 + random.uniform(-12, 12)
    pts += [(W+30, ebase(W+30)), (W+30, H)]
    out.append("fill %s stroke none polygon %s" % (color, " ".join("%.1f,%.1f" % p for p in pts)))

def hill(crest_x, crest_y, color):
    pts = [(-30, H), (-30, crest_y+34)]
    for i in range(0, W+60, 24):
        x = i - 30
        y = crest_y + 30 - 30*math.exp(-((x-crest_x)/300.0)**2) + 6*math.sin(x/90.0)
        pts.append((x, y))
    pts += [(W+30, crest_y+34), (W+30, H)]
    out.append("fill %s stroke none polygon %s" % (color, " ".join("%.1f,%.1f" % p for p in pts)))

def figure(cx, fy, h, col, raise_arm=False):
    sh_y = fy - h*0.74
    hw = h*0.17
    out.append("fill %s stroke none polygon %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f"
               % (col, cx-h*0.055, sh_y, cx+h*0.055, sh_y, cx+hw, fy, cx, fy+h*0.012, cx-hw, fy))
    out.append("fill %s stroke none polygon %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f"
               % (col, cx-h*0.022, sh_y-h*0.02, cx+h*0.022, sh_y-h*0.02, cx+h*0.02, sh_y-h*0.09, cx-h*0.02, sh_y-h*0.09))
    out.append("fill %s stroke none ellipse %.1f,%.1f %.1f,%.1f 0,360" % (col, cx, sh_y-h*0.135, h*0.072, h*0.085))
    if raise_arm:
        hx, hy = cx - h*0.045, sh_y - h*0.42
        out.append("fill %s stroke none polygon %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f"
                   % (col, cx-h*0.02, sh_y, cx+h*0.05, sh_y-h*0.01,
                      hx+h*0.035, hy+h*0.03, hx-h*0.02, hy+h*0.01))
        out.append("fill %s stroke none ellipse %.1f,%.1f %.1f,%.1f 0,360"
                   % (col, hx, hy, h*0.032, h*0.032))
    else:
        out.append("fill %s stroke none polygon %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f"
                   % (col, cx-h*0.05, sh_y+h*0.01, cx-h*0.02, sh_y+h*0.01,
                      cx-h*0.03, sh_y+h*0.30, cx-h*0.07, sh_y+h*0.29))

ridge(H*0.50, "#aeb7d0", H*0.20, 11)
ridge(H*0.62, "#8b99bd", H*0.24, 23)
ridge(H*0.74, "#5f6f9b", H*0.22, 31)
hill(778, int(H*0.80), "#39456a")
figure(740, int(H*0.80)+22, 96, "#28304c", raise_arm=False)
figure(800, int(H*0.80)+20, 112, "#222a44", raise_arm=True)

sys.stdout.write(" ".join(out))
PY
)
convert "$w/sky2.png" -draw "$scene" "$w/scene.png"

# single thin gold North Star
convert -size 240x240 xc:none -fill '#ffe7b4' -draw "circle 120,120 120,134" -blur 0x12 "$w/sglow.png"
convert -size 240x240 xc:none -stroke '#f4d684' -fill '#f4d684' \
    -strokewidth 2.4 -draw "line 120,18 120,222" -strokewidth 2.4 -draw "line 18,120 222,120" \
    -strokewidth 1.2 -draw "line 64,64 176,176" -strokewidth 1.2 -draw "line 176,64 64,176" -blur 0x0.5 "$w/sspk.png"
convert "$w/sglow.png" "$w/sspk.png" -compose screen -composite -resize 132x132 "$w/star.png"

# text labels — light weight, slate with a gold accent
convert -background none -font "$fr" -pointsize 54 -kerning 1 -fill '#33405e' label:'Nordstjernen ' "$w/t1.png"
convert -background none -font "$fr" -pointsize 54 -fill '#c2873a' label:"$ver" "$w/t2.png"
convert -background none -font "$fr" -pointsize 25 -fill '#586698' label:'Nordstjernen Web Browser' "$w/ts.png"
convert -background none -font "$fr" -pointsize 23 -kerning 3 -fill '#c2873a' label:"$codename" "$w/tc.png"
convert -background none -font "$fr" -pointsize 20 -fill '#727da8' \
    label:'Étoile du Nord — the legendary web browser' "$w/t3.png"

h1=$(identify -format '%h' "$w/t1.png"); w1=$(identify -format '%w' "$w/t1.png")
hs=$(identify -format '%h' "$w/ts.png"); hc=$(identify -format '%h' "$w/tc.png"); h3=$(identify -format '%h' "$w/t3.png")
g1=14; g2=14; g3=12
sy=$((46 + h1 + g1)); cy=$((sy + hs + g2)); gy=$((cy + hc + g3))
ty=46; textleft=80

convert "$w/scene.png" \
    \( "$w/star.png" \) -gravity NorthWest -geometry +726+18 -compose over -composite \
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
