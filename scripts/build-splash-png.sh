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

# two women reaching for the North Star — flat-shaded vector figures.
wfig=$(python3 - <<'PY'
import sys, math

W, H = 640, 980

def hexs(c): return "#%02x%02x%02x" % (int(c[0]), int(c[1]), int(c[2]))
def dk(c, f=0.80): return tuple(max(0, int(v*f)) for v in c)
def lt(c, f=1.16): return tuple(min(255, int(v*f)) for v in c)

OPS = []   # silhouette + detail ops, rendered for rim then normal

def P(col, pts, sil=True, alpha=None, stroke=None, sw=1.0):
    OPS.append(("poly", col, list(pts), sil, alpha, stroke, sw))
def E(col, cx, cy, rx, ry, sil=True, alpha=None):
    OPS.append(("ell", col, (cx, cy, rx, ry), sil, alpha, None, 0))
def PATH(col, d, sw, sil=False):
    OPS.append(("path", col, d, sil, None, None, sw))

def limb(p0, p1, p2, w0, w1, w2, col, sil=True):
    def seg(a, b, wa, wb):
        dx, dy = b[0]-a[0], b[1]-a[1]; L = math.hypot(dx, dy) or 1.0
        nx, ny = -dy/L, dx/L
        return [(a[0]+nx*wa, a[1]+ny*wa), (b[0]+nx*wb, b[1]+ny*wb),
                (b[0]-nx*wb, b[1]-ny*wb), (a[0]-nx*wa, a[1]-ny*wa)]
    P(col, seg(p0, p1, w0, w1), sil); P(col, seg(p1, p2, w1, w2), sil)
    E(col, p1[0], p1[1], w1, w1, sil)

def woman(cx, fy, h, skin, hair, dress, sash, style, gaze=-1, raise_arm=None):
    skin_sh = dk(skin, 0.86); dress_sh = dk(dress, 0.74); dress_hi = lt(dress, 1.14)
    top = fy - h
    hrx, hry = h*0.050, h*0.062
    hcx = cx + gaze*h*0.018
    hcy = top + hry + h*0.010
    neck_w = h*0.024
    sh_y = hcy + hry + h*0.058
    sh_w = h*0.100
    waist_y = sh_y + h*0.150
    waist_w = h*0.058
    hip_y = waist_y + h*0.060
    hem_y = fy - h*0.030
    hem_w = h*0.150
    aw = h*0.024

    # flowing back hair, sweeping to one side
    if style != "updo":
        P(dk(hair, 0.9), [
            (hcx-hrx*1.0, hcy-hry*0.3), (hcx-hrx*1.35, sh_y),
            (hcx-hrx*1.15, waist_y), (hcx-hrx*1.5, hip_y+h*0.04),
            (hcx-hrx*0.6, hip_y+h*0.02), (hcx-hrx*0.1, waist_y),
            (hcx+hrx*0.5, hip_y), (hcx+hrx*1.25, waist_y),
            (hcx+hrx*1.3, sh_y), (hcx+hrx*1.0, hcy-hry*0.3)])

    # gown: fitted bodice to a flared skirt, slight asymmetric sweep
    gown = [
        (cx-sh_w*0.92, sh_y), (cx-waist_w, waist_y), (cx-waist_w*1.05, hip_y),
        (cx-hem_w, hem_y-h*0.02), (cx-hem_w*0.95, hem_y), (cx-hem_w*0.2, hem_y+h*0.01),
        (cx+hem_w*0.55, hem_y+h*0.012), (cx+hem_w, hem_y-h*0.015),
        (cx+waist_w*1.05, hip_y), (cx+waist_w, waist_y), (cx+sh_w*0.92, sh_y),
        (cx+sh_w*0.40, sh_y-h*0.010), (cx, sh_y+h*0.026), (cx-sh_w*0.40, sh_y-h*0.010)]
    P(dress, gown)
    # form shading on shadow side
    P(dress_sh, [(cx, sh_y+h*0.026), (cx, hem_y+h*0.008), (cx+hem_w*0.55, hem_y+h*0.012),
                 (cx+hem_w, hem_y-h*0.015), (cx+waist_w*1.05, hip_y),
                 (cx+waist_w, waist_y), (cx+sh_w*0.40, sh_y-h*0.010)], sil=False, alpha=0.5)
    # lit highlight fold
    P(dress_hi, [(cx-sh_w*0.18, sh_y+h*0.035), (cx-waist_w*0.5, waist_y),
                 (cx-hem_w*0.42, hem_y-h*0.01), (cx-hem_w*0.24, hem_y-h*0.01),
                 (cx-waist_w*0.18, waist_y), (cx-sh_w*0.02, sh_y+h*0.035)], sil=False, alpha=0.32)
    # sash at the waist
    P(sash, [(cx-waist_w*1.04, waist_y), (cx+waist_w*1.04, waist_y),
             (cx+waist_w*1.16, hip_y), (cx-waist_w*1.16, hip_y)], sil=False)
    P(dk(sash, 0.7), [(cx, waist_y+h*0.004), (cx+waist_w*1.12, hip_y),
             (cx+waist_w*1.16, hip_y), (cx+waist_w*1.04, waist_y)], sil=False, alpha=0.5)

    # shoe tips
    for s in (-1, 1):
        sx = cx + s*h*0.040
        P((46, 42, 56), [(sx-h*0.026, hem_y+h*0.008), (sx+h*0.028, hem_y+h*0.008),
                         (sx+h*0.032, fy), (sx-h*0.022, fy)])

    # arms
    sL = (cx-sh_w*0.84, sh_y+h*0.006); sR = (cx+sh_w*0.84, sh_y+h*0.006)
    if raise_arm == "L":
        elb = (cx-sh_w*1.45, sh_y-h*0.085); wr = (cx-sh_w*1.95, sh_y-h*0.235)
        limb(sL, elb, wr, aw*1.05, aw*0.85, aw*0.62, skin)
        E(skin, wr[0]-aw*0.2, wr[1]-aw*0.3, aw*0.85, aw*0.95)        # hand
        # the other arm rests down/inward
        limb(sR, (cx+waist_w*1.2, waist_y), (cx+waist_w*0.7, hip_y+h*0.02), aw*1.05, aw*0.8, aw*0.62, skin)
        E(skin, cx+waist_w*0.6, hip_y+h*0.04, aw*0.8, aw*0.85)
    else:
        limb(sL, (cx-waist_w*1.2, waist_y), (cx-waist_w*0.85, hip_y+h*0.02), aw*1.05, aw*0.8, aw*0.62, skin)
        E(skin, cx-waist_w*0.75, hip_y+h*0.04, aw*0.8, aw*0.85)
        # inner hand to chest
        limb(sR, (cx+sh_w*0.5, waist_y-h*0.02), (cx+sh_w*0.08, waist_y-h*0.05), aw*1.05, aw*0.78, aw*0.6, skin)
        E(skin, cx+sh_w*0.0, waist_y-h*0.055, aw*0.78, aw*0.82)

    # neck
    P(skin_sh, [(hcx-neck_w, hcy+hry*0.5), (hcx+neck_w, hcy+hry*0.5),
                (hcx+neck_w*0.9, sh_y+h*0.002), (hcx-neck_w*0.9, sh_y+h*0.002)])

    # head
    E(skin, hcx, hcy, hrx, hry)

    # front hair framing + crown
    if style == "updo":
        E(hair, hcx, hcy-hry*0.82, hrx*0.62, hry*0.5)
        E(dk(hair, 0.88), hcx+hrx*0.5, hcy-hry*1.02, hrx*0.3, hry*0.26)
        P(hair, [(hcx-hrx*1.05, hcy+hry*0.18), (hcx-hrx*1.05, hcy-hry*0.5),
                 (hcx-hrx*0.42, hcy-hry*1.16), (hcx+hrx*0.42, hcy-hry*1.16),
                 (hcx+hrx*1.05, hcy-hry*0.5), (hcx+hrx*1.05, hcy+hry*0.18),
                 (hcx+hrx*0.8, hcy-hry*0.06), (hcx+hrx*0.58, hcy-hry*0.52),
                 (hcx-hrx*0.58, hcy-hry*0.52), (hcx-hrx*0.8, hcy-hry*0.06)])
    else:
        P(hair, [(hcx-hrx*1.14, hcy+hry*0.62), (hcx-hrx*1.18, hcy-hry*0.5),
                 (hcx-hrx*0.5, hcy-hry*1.2), (hcx+hrx*0.5, hcy-hry*1.2),
                 (hcx+hrx*1.18, hcy-hry*0.5), (hcx+hrx*1.14, hcy+hry*0.62),
                 (hcx+hrx*0.84, hcy+hry*0.05), (hcx+hrx*0.6, hcy-hry*0.56),
                 (hcx+hrx*0.2, hcy-hry*0.88), (hcx-hrx*0.2, hcy-hry*0.88),
                 (hcx-hrx*0.6, hcy-hry*0.56), (hcx-hrx*0.84, hcy+hry*0.05)])
    PATH(lt(hair, 1.45), "M %.1f,%.1f Q %.1f,%.1f %.1f,%.1f" % (
        hcx-hrx*0.72, hcy-hry*0.55, hcx-hrx*1.0, hcy, hcx-hrx*0.8, hcy+hry*0.35), 2.2)

    # face (looking up: features set a little high)
    ey = hcy - hry*0.04
    E((42, 34, 40), hcx-hrx*0.33, ey, hrx*0.09, hry*0.11, sil=False)
    E((42, 34, 40), hcx+hrx*0.33, ey, hrx*0.09, hry*0.11, sil=False)
    E((250, 252, 255), hcx-hrx*0.30, ey-hry*0.02, hrx*0.03, hry*0.035, sil=False)
    E((250, 252, 255), hcx+hrx*0.36, ey-hry*0.02, hrx*0.03, hry*0.035, sil=False)
    E((230, 140, 138), hcx-hrx*0.56, ey+hry*0.30, hrx*0.16, hry*0.10, sil=False, alpha=0.5)
    E((230, 140, 138), hcx+hrx*0.56, ey+hry*0.30, hrx*0.16, hry*0.10, sil=False, alpha=0.5)
    PATH(dk(skin, 0.55), "M %.1f,%.1f Q %.1f,%.1f %.1f,%.1f" % (
        hcx-hrx*0.22, ey+hry*0.46, hcx, ey+hry*0.60, hcx+hrx*0.22, ey+hry*0.46), 2.2)


# Woman B (right, behind) drawn first; updo, emerald gown, deeper skin
woman(388, 858, 590, (172, 120, 86), (30, 24, 26), (52, 168, 150), (236, 196, 92),
      "updo", gaze=-1, raise_arm=None)
# Woman A (left, front, taller); flowing hair, rose gown, reaching toward the star
woman(238, 866, 642, (236, 190, 158), (74, 46, 38), (214, 78, 96), (240, 206, 120),
      "wavy", gaze=-1, raise_arm="L")

# sparkles near the raised hand (toward the North Star)
for (sx, sy, r) in [(70, 250, 5.0), (104, 300, 3.0), (150, 235, 3.6), (52, 320, 2.4)]:
    OPS.append(("spark", (210, 246, 255), (sx, sy, r), False, None, None, 0))


def emit(dx, dy, rim=None, only_sil=False):
    s = []
    for kind, col, geom, sil, alpha, stroke, sw in OPS:
        if only_sil and not sil:
            continue
        c = rim if rim else col
        if kind == "poly":
            pts = " ".join("%.1f,%.1f" % (p[0]+dx, p[1]+dy) for p in geom)
            fill = ("rgba(%d,%d,%d,%.2f)" % (c[0], c[1], c[2], alpha)) if alpha is not None else hexs(c)
            s.append("stroke none fill %s polygon %s" % (fill, pts))
        elif kind == "ell":
            cx, cy, rx, ry = geom
            fill = ("rgba(%d,%d,%d,%.2f)" % (c[0], c[1], c[2], alpha)) if alpha is not None else hexs(c)
            s.append("stroke none fill %s ellipse %.1f,%.1f %.1f,%.1f 0,360" % (fill, cx+dx, cy+dy, rx, ry))
        elif kind == "path":
            s.append("stroke %s stroke-width %.1f fill none path '%s'" % (hexs(c), sw, _shift_path(geom, dx, dy)))
        elif kind == "spark":
            cx, cy, r = geom
            s.append("stroke none fill %s polygon %s" % (hexs(c),
                " ".join("%.1f,%.1f" % p for p in _star(cx+dx, cy+dy, r))))
    return " ".join(s)

def _shift_path(d, dx, dy):
    out = []
    for tok in d.split():
        if "," in tok:
            x, y = tok.split(","); out.append("%.1f,%.1f" % (float(x)+dx, float(y)+dy))
        else:
            out.append(tok)
    return " ".join(out)

def _star(cx, cy, r):
    pts = []
    for k in range(8):
        ang = math.pi*k/4
        rr = r if k % 2 == 0 else r*0.4
        pts.append((cx+rr*math.cos(ang), cy+rr*math.sin(ang)))
    return pts

rim = emit(-3, -3, rim=(140, 226, 250), only_sil=True)
body = emit(0, 0)
sys.stdout.write(rim + " " + body)

PY
)
convert -size 640x980 xc:none -draw "$wfig" -trim +repage -resize x266 "$w/women.png"

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
    \( "$w/women.png" \) -gravity NorthEast -geometry +28+10 -compose over -composite \
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
