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
    /usr/share/fonts/dejavu/DejaVuSans-Bold.ttf \
    /usr/share/fonts/TTF/DejaVuSans-Bold.ttf)
fr=$(find_font 'DejaVu Sans' \
    /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
    /usr/share/fonts/dejavu/DejaVuSans.ttf \
    /usr/share/fonts/TTF/DejaVuSans.ttf)

w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT

# text labels (auto-sized)
convert -background none -font "$fb" -pointsize 54 -fill '#ffffff' label:'Nordstjernen ' "$w/t1.png"
convert -background none -font "$fb" -pointsize 54 -fill '#7fb0ff' label:"$ver" "$w/t2.png"
convert -background none -font "$fb" -pointsize 25 -kerning 3 -fill '#ffd27a' label:"$codename" "$w/tc.png"
convert -background none -font "$fr" -pointsize 21 -fill '#cdd8ee' \
    label:'Étoile du Nord — the legendary web browser' "$w/t3.png"
w1=$(identify -format '%w' "$w/t1.png"); h1=$(identify -format '%h' "$w/t1.png")
w2=$(identify -format '%w' "$w/t2.png")
textleft=210; rm=70
titlew=$((textleft + w1 + w2)); cw=$((titlew + rm)); [ "$cw" -lt 940 ] && cw=940; ch=310

# night sky
convert -size ${cw}x${ch} gradient:'#0a1430'-'#1b2c66' "$w/sky.png"

# real aurora: green/cyan vertical-ray curtains, waved and blurred
ha=200
convert -size ${cw}x1 xc: +noise Random -colorspace Gray -scale ${cw}x${ha}\! \
    -level 42%,100% -gamma 1.5 "$w/rays.png"
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

# Frontier: Elite 2 style flat-shaded ship (wedge, pointing right) with engine trail
convert -size 250x140 xc:none -stroke '#2a2f3d' -strokewidth 1.4 \
    -fill '#7e8aa6' -draw "polygon 214,70 150,46 96,34 52,14 82,52 34,58 28,70 34,82 82,88 52,126 96,106 150,94" \
    -fill '#aeb8d0' -draw "polygon 214,70 150,52 98,48 62,70 98,92 150,88" \
    -fill '#59617c' -draw "polygon 96,34 52,14 82,52 98,48" \
    -fill '#59617c' -draw "polygon 96,106 52,126 82,88 98,92" \
    -fill '#1f3650' -stroke none -draw "polygon 178,66 150,60 150,80 178,74" "$w/ship0.png"
convert "$w/ship0.png" -fill 'rgba(130,235,255,0.95)' \
    -draw "circle 31,64 31,67" -draw "circle 31,76 31,79" "$w/ship1.png"
convert -size 250x140 xc:none -fill 'rgba(150,240,255,0.55)' \
    -draw "polygon 30,66 30,74 -40,72 -40,68" -blur 0x6 "$w/trail.png"
convert "$w/trail.png" "$w/ship1.png" -compose over -composite -resize 172x96 "$w/ship.png"

# compose: sky + aurora + stars + star + ship + text
convert "$w/sky.png" \
    \( "$w/aurora.png" -channel A -evaluate multiply 0.82 +channel -gravity North -geometry +50+14 \) \
    -compose screen -composite "$w/sky2.png"
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
ty=86; cy=$((ty + h1 + 18)); gy=$((cy + 44))
convert "$w/sky3.png" \
    \( "$w/star.png" \) -gravity NorthWest -geometry +35+78 -compose screen -composite \
    \( "$w/ship.png" \) -gravity NorthEast -geometry +45+18 -compose over -composite \
    "$w/t1.png" -gravity NorthWest -geometry +${textleft}+${ty} -compose over -composite \
    "$w/t2.png" -gravity NorthWest -geometry +$((textleft + w1))+${ty} -compose over -composite \
    "$w/tc.png" -gravity NorthWest -geometry +${textleft}+${cy} -compose over -composite \
    "$w/t3.png" -gravity NorthWest -geometry +${textleft}+${gy} -compose over -composite \
    "$w/full.png"
convert "$w/full.png" -strip -dither FloydSteinberg -colors 220 \
    -define png:compression-level=9 PNG8:"$w/splash.png"
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
