#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ver=$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -n1)
[ -n "$ver" ] || { echo "could not read version from meson.build" >&2; exit 1; }

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

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

convert -background none -font "$fb" -pointsize 58 -fill '#ffffff' label:'Nordstjernen ' "$work/t1.png"
convert -background none -font "$fb" -pointsize 58 -fill '#7fb0ff' label:"$ver" "$work/t2.png"
convert -background none -font "$fr" -pointsize 22 -fill '#cdd8ee' \
    label:'Étoile du Nord — the legendary web browser, built in Norway' "$work/t3.png"
w1=$(identify -format '%w' "$work/t1.png"); h1=$(identify -format '%h' "$work/t1.png")
w2=$(identify -format '%w' "$work/t2.png")
w3=$(identify -format '%w' "$work/t3.png")

textleft=215; rm=60
titlew=$((textleft + w1 + w2)); linew=$((textleft + w3))
cw=$(( titlew > linew ? titlew : linew )); cw=$((cw + rm)); ch=300

convert -size ${cw}x${ch} gradient:'#0a1430'-'#1b2c66' "$work/bg.png"
convert -size ${cw}x${ch} xc:none \
    -fill 'rgba(70,210,170,0.22)' -draw "polygon 0,225 ${cw},150 ${cw},205 0,270" \
    -fill 'rgba(110,150,240,0.18)' -draw "polygon 0,190 ${cw},105 ${cw},150 0,230" \
    -blur 0x26 "$work/aurora.png"

stars=$(python3 - "$cw" "$ch" <<'PY'
import sys, random
W, H = int(sys.argv[1]), int(sys.argv[2])
random.seed(11)
out = []
for _ in range(170):
    x = random.randint(0, W); y = random.randint(0, H - 40)
    r = random.choice([0.4, 0.6, 0.6, 0.8, 1.0, 1.3])
    op = round(random.uniform(0.25, 0.95), 2)
    out.append("fill rgba(255,255,255,%s) circle %d,%d %.1f,%d" % (op, x, y, x + r, y))
for x, y, r in [(int(W * 0.8), 70, 2.0), (int(W * 0.55), 215, 1.6), (int(W * 0.9), 225, 1.7)]:
    out.append("fill rgba(150,190,255,0.16) circle %d,%d %.1f,%d" % (x, y, x + r * 5, y))
    out.append("fill rgba(255,255,255,0.98) circle %d,%d %.1f,%d" % (x, y, x + r, y))
print(" ".join(out))
PY
)
convert "$work/bg.png" "$work/aurora.png" -compose over -composite -draw "$stars" "$work/base.png"

convert -size 200x200 xc:none -fill white -draw "circle 100,100 100,108" -blur 0x7 "$work/glow.png"
convert -size 200x200 xc:none -stroke white -fill white \
    -strokewidth 2 -draw "line 100,25 100,175" -strokewidth 2 -draw "line 25,100 175,100" \
    -strokewidth 1 -draw "line 55,55 145,145" -strokewidth 1 -draw "line 145,55 55,145" \
    -blur 0x0.6 "$work/spikes.png"
convert "$work/glow.png" "$work/spikes.png" -compose screen -composite -resize 150x150 "$work/star.png"

ty=112; gy=$((ty + h1 + 12))
convert "$work/base.png" \
    "$work/star.png" -gravity NorthWest -geometry +35+72 -compose screen -composite \
    "$work/t1.png" -gravity NorthWest -geometry +${textleft}+${ty} -compose over -composite \
    "$work/t2.png" -gravity NorthWest -geometry +$((textleft + w1))+${ty} -compose over -composite \
    "$work/t3.png" -gravity NorthWest -geometry +${textleft}+${gy} -compose over -composite \
    "$work/full.png"
convert "$work/full.png" -strip -dither FloydSteinberg -colors 200 \
    -define png:compression-level=9 PNG8:"$work/splash.png"
echo "rendered splash ${cw}x${ch} for $ver ($(stat -c%s "$work/splash.png") bytes)"

header="src/about_splash_png.h"
python3 - "$work/splash.png" "$header" <<'PY'
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
