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

# advanced Frontier-style ship with an engine plasma flare
sc=470; sh=200
convert -size ${sc}x${sh} xc:none -fill 'rgba(185,90,255,0.30)' \
    -draw "polygon 134,82 134,118 14,134 -10,100 14,66" -blur 0x17 "$w/f_mag.png"
convert -size ${sc}x${sh} xc:none -fill 'rgba(70,195,255,0.62)' \
    -draw "polygon 134,80 134,120 24,140 -2,100 24,60" -blur 0x14 "$w/f_out.png"
convert -size ${sc}x${sh} xc:none -fill 'rgba(135,228,255,0.88)' \
    -draw "polygon 134,86 134,114 44,126 22,100 44,74" -blur 0x7 "$w/f_mid.png"
convert -size ${sc}x${sh} xc:none -fill 'rgba(240,252,255,0.96)' \
    -draw "polygon 134,90 134,110 72,116 52,100 72,84" -blur 0x3 "$w/f_core.png"
convert "$w/f_mag.png" "$w/f_out.png" -compose screen -composite \
    "$w/f_mid.png" -compose screen -composite "$w/f_core.png" -compose screen -composite "$w/flare.png"
convert -size ${sc}x${sh} xc:none -stroke '#252a38' -strokewidth 1.5 \
    -fill '#828ea9' -draw "polygon 410,100 340,83 278,71 210,51 156,33 196,77 150,79 134,87 130,100 134,113 150,121 196,123 156,167 210,149 278,129 340,117" \
    -fill '#c4cee0' -draw "polygon 410,100 322,86 150,84 140,100 150,116 322,114" \
    -fill '#565f79' -draw "polygon 210,51 156,33 196,77 150,79 210,51" \
    -fill '#565f79' -draw "polygon 210,149 156,167 196,123 150,121 210,149" \
    -fill '#3a4156' -draw "polygon 196,77 150,79 134,87 130,100 134,113 150,121 196,123 168,100" "$w/hull.png"
convert "$w/hull.png" -stroke '#2b3142' -strokewidth 1 -fill none \
    -draw "line 322,86 205,85" -draw "line 322,114 205,115" -draw "line 300,100 165,100" "$w/hull2.png"
convert "$w/hull2.png" -stroke none \
    -fill '#10131c' -draw "circle 131,90 131,92" -draw "circle 131,100 131,102" -draw "circle 131,110 131,112" \
    -fill '#bdeeff' -draw "circle 131,90 131,90.8" -draw "circle 131,100 131,100.8" -draw "circle 131,110 131,110.8" "$w/hull3.png"
convert -size ${sc}x${sh} xc:none -fill 'rgba(120,215,255,0.85)' \
    -draw "polygon 352,100 320,89 296,100 320,111" -blur 0x1.4 "$w/canopy.png"
convert "$w/canopy.png" -stroke none -fill 'rgba(238,251,255,0.95)' \
    -draw "polygon 338,100 320,93 308,100 320,107" "$w/canopy2.png"
convert -size ${sc}x${sh} xc:none \
    -fill 'rgba(120,255,150,0.97)' -draw "circle 156,33 156,35.4" \
    -fill 'rgba(255,95,95,0.97)' -draw "circle 156,167 156,169.4" -blur 0x0.7 "$w/lights.png"
convert "$w/hull3.png" "$w/canopy2.png" -compose screen -composite \
    "$w/lights.png" -compose screen -composite "$w/shiponly.png"
convert "$w/flare.png" "$w/shiponly.png" -compose over -composite -resize 230x98 "$w/ship.png"

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
    \( "$w/ship.png" \) -gravity NorthEast -geometry +26+10 -compose over -composite \
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
