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

# the scene is composed at 2x and downscaled once for clean anti-aliased edges
S=2
W=$((940 * S)); H=$((320 * S))

# golden-age sky: deep blue at the top warming to pale gold at the horizon
convert -size ${W}x${H} gradient:'#2f74b4'-'#f4ead0' "$w/sky.png"
convert -size ${W}x${H} gradient:none-'#fbeecb' "$w/warm.png"
convert "$w/sky.png" "$w/warm.png" -compose over -composite "$w/sky1.png"

# a soft warm halo behind the world globe (upper right)
convert -size ${W}x${H} xc:none -fill '#cfe9ff' \
    -draw "ellipse $((760*S)),$((72*S)) $((150*S)),$((120*S)) 0,360" -blur 0x$((70*S)) "$w/glow.png"
convert "$w/sky1.png" "$w/glow.png" -compose screen -composite "$w/sky2.png"

# soft clouds drifting across the horizon
convert "$w/sky2.png" -fill 'rgba(255,255,255,0.82)' \
    -draw "ellipse $((250*S)),$((104*S)) $((50*S)),$((16*S)) 0,360 ellipse $((300*S)),$((110*S)) $((42*S)),$((18*S)) 0,360 ellipse $((205*S)),$((114*S)) $((30*S)),$((12*S)) 0,360" \
    -draw "ellipse $((560*S)),$((56*S)) $((40*S)),$((13*S)) 0,360 ellipse $((602*S)),$((62*S)) $((30*S)),$((12*S)) 0,360" \
    -draw "ellipse $((150*S)),$((150*S)) $((36*S)),$((12*S)) 0,360 ellipse $((190*S)),$((156*S)) $((28*S)),$((11*S)) 0,360" \
    -blur 0x$((2*S)) "$w/sky3.png"

# the world globe + air traffic: airliner with contrail, biplane, balloon, rocket, birds
skydraw=$(python3 - "$W" "$H" "$S" <<'PY'
import sys, math
W, H = int(sys.argv[1]), int(sys.argv[2])
S = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
out = []

def hx(c): return "#%02x%02x%02x" % (int(c[0]), int(c[1]), int(c[2]))
def dk(c, f=0.8): return tuple(max(0, int(v*f)) for v in c)
def lt(c, f=1.15): return tuple(min(255, int(v*f)) for v in c)
def poly(col, pts):
    out.append("fill %s stroke none polygon %s" % (hx(col), " ".join("%.2f,%.2f" % p for p in pts)))
def rgba_poly(rgba, pts):
    out.append("fill %s stroke none polygon %s" % (rgba, " ".join("%.2f,%.2f" % p for p in pts)))
def ell(col, cx, cy, rx, ry):
    out.append("fill %s stroke none ellipse %.2f,%.2f %.2f,%.2f 0,360" % (hx(col), cx, cy, rx, ry))
def rgba_ell(rgba, cx, cy, rx, ry, a0=0, a1=360):
    out.append("fill %s stroke none ellipse %.2f,%.2f %.2f,%.2f %d,%d" % (rgba, cx, cy, rx, ry, a0, a1))
def arc(col, wid, cx, cy, rx, ry, a0, a1):
    out.append("fill none stroke %s stroke-width %.2f ellipse %.2f,%.2f %.2f,%.2f %d,%d" % (col, wid, cx, cy, rx, ry, a0, a1))
    out.append("stroke none")
def line(col, wid, a, b):
    out.append("stroke %s stroke-width %.2f stroke-linecap round line %.2f,%.2f %.2f,%.2f" % (hx(col), wid, a[0], a[1], b[0], b[1]))
    out.append("stroke none")

def globe(cx, cy, R):
    rgba_ell("rgba(20,60,110,0.35)", cx+R*0.10, cy+R*0.12, R*1.02, R*1.02)
    ell((44,116,176), cx, cy, R, R)
    ell((66,150,206), cx-R*0.16, cy-R*0.18, R*0.84, R*0.84)
    land = (86,170,96)
    conts = [(-0.34,-0.30,0.30,0.24),(-0.10,-0.05,0.26,0.34),(-0.22,0.38,0.20,0.22),
             (0.30,-0.28,0.26,0.20),(0.40,0.16,0.22,0.30),(0.06,0.52,0.14,0.12),
             (-0.52,0.10,0.13,0.20)]
    for dx,dy,rx,ry in conts:
        d = math.hypot(dx, dy)
        if d+max(rx,ry)*0.5 > 0.98: continue
        ell(dk(land,0.9), cx+dx*R+R*0.03, cy+dy*R+R*0.03, rx*R, ry*R)
    for dx,dy,rx,ry in conts:
        d = math.hypot(dx, dy)
        if d+max(rx,ry)*0.5 > 0.98: continue
        ell(land, cx+dx*R, cy+dy*R, rx*R, ry*R)
    grat = "rgba(255,255,255,0.22)"
    for k in (0.85, 0.55, 0.0):
        arc(grat, max(1.0,1.0*S), cx, cy, R*0.985, R*0.985*k, 0, 360)
    for k in (0.9, 0.5):
        arc(grat, max(1.0,1.0*S), cx, cy, R*0.985*k, R*0.985, 0, 360)
    rgba_ell("rgba(6,30,66,0.30)", cx, cy, R*0.985, R*0.985, 292, 68)
    rgba_ell("rgba(255,255,255,0.60)", cx-R*0.40, cy-R*0.42, R*0.22, R*0.16)
    arc("rgba(210,236,255,0.55)", max(1.0,1.4*S), cx, cy, R*0.97, R*0.97, 150, 250)

def airliner(cx, cy, s):
    body = (238,241,247); trim = (70,120,210); dark = (52,62,86)
    poly(dk(body,0.9), [(cx-s*2.0, cy+s*0.20), (cx+s*1.5, cy+s*0.24), (cx+s*2.05, cy), (cx+s*1.5, cy-s*0.16)])
    ell(body, cx, cy, s*1.9, s*0.34)
    poly(body, [(cx+s*1.4, cy), (cx+s*2.35, cy-s*0.05), (cx+s*1.5, cy+s*0.05)])
    poly(dk(body,0.82), [(cx-s*1.7, cy-s*0.10), (cx-s*2.5, cy-s*0.72), (cx-s*2.05, cy-s*0.72), (cx-s*1.15, cy-s*0.08)])
    poly(dk(body,0.86), [(cx+s*0.55, cy+s*0.14), (cx-s*0.35, cy+s*1.05), (cx-s*0.95, cy+s*1.05), (cx-s*0.30, cy+s*0.18)])
    poly(lt(body,1.02), [(cx+s*0.7, cy-s*0.12), (cx-s*0.2, cy-s*0.98), (cx-s*0.75, cy-s*0.98), (cx-s*0.1, cy-s*0.16)])
    out.append("fill %s stroke none rectangle %.2f,%.2f %.2f,%.2f" % (hx(trim), cx-s*1.6, cy-s*0.06, cx+s*1.4, cy+s*0.04))
    for i in range(6):
        ell(dark, cx+s*(1.0-i*0.42), cy-s*0.02, s*0.06, s*0.06)

def biplane(cx, cy, s):
    body = (226,86,72); cream = (240,228,196); dark = (58,44,40)
    line(dark, max(1.0,1.2*S), (cx-s*1.2, cy-s*0.55), (cx-s*1.2, cy+s*0.55))
    line(dark, max(1.0,1.2*S), (cx-s*0.5, cy-s*0.55), (cx-s*0.5, cy+s*0.55))
    poly(cream, [(cx-s*1.9, cy-s*0.62), (cx+s*0.6, cy-s*0.62), (cx+s*0.6, cy-s*0.48), (cx-s*1.9, cy-s*0.48)])
    ell(body, cx, cy, s*1.5, s*0.3)
    poly(cream, [(cx-s*1.9, cy+s*0.48), (cx+s*0.6, cy+s*0.48), (cx+s*0.6, cy+s*0.62), (cx-s*1.9, cy+s*0.62)])
    poly(body, [(cx-s*1.35, cy-s*0.06), (cx-s*2.0, cy-s*0.5), (cx-s*1.75, cy-s*0.5), (cx-s*1.0, cy-s*0.04)])
    line(dark, max(1.4,1.8*S), (cx+s*1.45, cy-s*0.42), (cx+s*1.45, cy+s*0.42))
    ell(lt(cream,1.05), cx-s*0.2, cy-s*0.05, s*0.28, s*0.16)

def balloon(cx, cy, r, c1, c2):
    poly(dk(c1,0.9), [(cx-r*0.55, cy+r*0.55), (cx+r*0.55, cy+r*0.55), (cx+r*0.16, cy+r*1.05), (cx-r*0.16, cy+r*1.05)])
    ell(c1, cx, cy, r, r*1.12)
    ell(c2, cx-r*0.33, cy, r*0.34, r*1.12)
    ell(c2, cx+r*0.33, cy, r*0.34, r*1.12)
    ell(lt(c1,1.18), cx-r*0.30, cy-r*0.45, r*0.22, r*0.30)
    line((90,64,40), max(1.0,1.2*S), (cx-r*0.32, cy+r*1.05), (cx-r*0.18, cy+r*1.45))
    line((90,64,40), max(1.0,1.2*S), (cx+r*0.32, cy+r*1.05), (cx+r*0.18, cy+r*1.45))
    poly((120,84,48), [(cx-r*0.20, cy+r*1.45), (cx+r*0.20, cy+r*1.45), (cx+r*0.16, cy+r*1.66), (cx-r*0.16, cy+r*1.66)])

def rocket(cx, by, s):
    body = (238,240,246); nose = (220,72,66); fin = (70,120,210)
    for i in range(7):
        rgba_ell("rgba(236,240,248,%.2f)" % (0.5 - i*0.06), cx+math.sin(i*1.1)*s*0.5, by+s*1.8+i*s*1.5, s*(0.7+i*0.16), s*(0.5+i*0.14))
    poly((255,196,74), [(cx-s*0.30, by+s*1.6), (cx+s*0.30, by+s*1.6), (cx, by+s*3.0)])
    poly((255,140,52), [(cx-s*0.18, by+s*1.6), (cx+s*0.18, by+s*1.6), (cx, by+s*2.4)])
    poly(fin, [(cx-s*0.42, by+s*1.7), (cx-s*0.42, by+s*1.0), (cx-s*0.18, by+s*1.55)])
    poly(fin, [(cx+s*0.42, by+s*1.7), (cx+s*0.42, by+s*1.0), (cx+s*0.18, by+s*1.55)])
    out.append("fill %s stroke none path 'M %.2f,%.2f L %.2f,%.2f Q %.2f,%.2f %.2f,%.2f Q %.2f,%.2f %.2f,%.2f Z'" % (
        hx(body), cx-s*0.28, by+s*1.7, cx-s*0.28, by+s*0.55,
        cx-s*0.28, by-s*0.5, cx, by-s*0.5,
        cx+s*0.28, by-s*0.5, cx+s*0.28, by+s*0.55, ))
    poly(nose, [(cx-s*0.28, by+s*0.4), (cx+s*0.28, by+s*0.4), (cx+s*0.28, by+s*0.62), (cx-s*0.28, by+s*0.62)])
    ell((120,170,220), cx, by+s*0.95, s*0.14, s*0.14)

def bird(cx, cy, s):
    line((58,72,92), max(1.0,1.6*S), (cx-s, cy+s*0.34), (cx, cy))
    line((58,72,92), max(1.0,1.6*S), (cx, cy), (cx+s, cy+s*0.34))

globe(W*0.80, H*0.235, H*0.185)

ax, ay = W*0.285, H*0.078
for i in range(15):
    t = i/14.0
    px = ax + (W*0.30)*t
    py = ay - math.sin(t*math.pi)*H*0.022 + t*H*0.010
    rgba_ell("rgba(255,255,255,%.2f)" % (0.08+0.40*t), px, py, W*0.012*(0.4+t), H*0.010*(0.4+t))
airliner(W*0.590, H*0.088, H*0.032)

biplane(W*0.625, H*0.150, H*0.031)

rocket(W*0.955, H*0.150, H*0.030)

bx, by = W*0.360, H*0.120
for i in range(3):
    bird(bx + i*W*0.018, by + i*H*0.013, H*0.013)
    bird(bx - i*W*0.018, by + i*H*0.013, H*0.013)

sys.stdout.write(" ".join(out))
PY
)
convert "$w/sky3.png" -draw "$skydraw" "$w/sky6.png"

# civilization panorama: coast of world wonders, a shore road with cars, ships at sea
scene=$(python3 - "$W" "$H" "$S" <<'PY'
import sys, random, math
W, H = int(sys.argv[1]), int(sys.argv[2])
S = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
out = []
random.seed(20)

def hx(c): return "#%02x%02x%02x" % (int(c[0]), int(c[1]), int(c[2]))
def dk(c, f=0.8): return tuple(max(0, int(v*f)) for v in c)
def lt(c, f=1.15): return tuple(min(255, int(v*f)) for v in c)
def poly(col, pts):
    out.append("fill %s stroke none polygon %s" % (hx(col), " ".join("%.2f,%.2f" % p for p in pts)))
def rgba_poly(rgba, pts):
    out.append("fill %s stroke none polygon %s" % (rgba, " ".join("%.2f,%.2f" % p for p in pts)))
def ell(col, cx, cy, rx, ry):
    out.append("fill %s stroke none ellipse %.2f,%.2f %.2f,%.2f 0,360" % (hx(col), cx, cy, rx, ry))
def rgba_ell(rgba, cx, cy, rx, ry, a0=0, a1=360):
    out.append("fill %s stroke none ellipse %.2f,%.2f %.2f,%.2f %d,%d" % (rgba, cx, cy, rx, ry, a0, a1))
def arc(col, wid, cx, cy, rx, ry, a0, a1):
    out.append("fill none stroke %s stroke-width %.2f ellipse %.2f,%.2f %.2f,%.2f %d,%d" % (col, wid, cx, cy, rx, ry, a0, a1))
    out.append("stroke none")
def line(col, wid, a, b):
    out.append("stroke %s stroke-width %.2f stroke-linecap round line %.2f,%.2f %.2f,%.2f" % (hx(col), wid, a[0], a[1], b[0], b[1]))
    out.append("stroke none")
def rect(col, x0, y0, x1, y1):
    out.append("fill %s stroke none rectangle %.2f,%.2f %.2f,%.2f" % (hx(col), x0, y0, x1, y1))

WL = H*0.705

# distant blue hills behind the wonders
def ridge(base_y, color, amp, seed, x0):
    random.seed(seed)
    pts = [(x0, H), (x0, base_y)]
    x = x0
    while x < W + 30*S:
        y = base_y - random.uniform(amp*0.3, amp)
        pts.append((x, y)); x += (70 + random.uniform(-18, 18))*S
    pts += [(W+30*S, base_y), (W+30*S, H)]
    poly(color, pts)

ridge(H*0.50, (150,178,196), H*0.09, 3, W*0.30)
ridge(H*0.56, (128,168,150), H*0.08, 6, W*0.28)

# the land: a promontory that meets the sea along the waterline on the right
land_pts = [(W*0.285, WL), (W*0.34, H*0.66), (W*0.42, H*0.615), (W*0.52, H*0.60),
            (W*0.60, H*0.58), (W*0.70, H*0.585), (W*0.80, H*0.55),
            (W*0.90, H*0.505), (W, H*0.47), (W, WL)]
poly((150,196,120), land_pts)
poly(dk((150,196,120),0.94), [(W*0.285, WL), (W*0.34, H*0.66), (W*0.42, H*0.615),
    (W*0.52, H*0.60), (W*0.60, H*0.58), (W*0.60, WL)])

# depth + shading helpers, then a hazy Greco-Roman skyline behind the wonders
def gshadow(cx, by, rx, ry, a=0.20):
    rgba_ell("rgba(46,58,44,%.2f)" % a, cx, by, rx, ry)

def tri_band(apex, bl, br, ct, cb, n=8):
    for i in range(n):
        t0=i/n; t1=(i+1)/n
        y0=apex[1]+(bl[1]-apex[1])*t0; y1=apex[1]+(bl[1]-apex[1])*t1
        xl0=apex[0]+(bl[0]-apex[0])*t0; xl1=apex[0]+(bl[0]-apex[0])*t1
        xr0=apex[0]+(br[0]-apex[0])*t0; xr1=apex[0]+(br[0]-apex[0])*t1
        tm=(t0+t1)/2
        c=tuple(int(ct[k]+(cb[k]-ct[k])*tm) for k in range(3))
        poly(c, [(xl0,y0),(xr0,y0),(xr1,y1),(xl1,y1)])

_lt=[(0.285,0.705),(0.34,0.66),(0.42,0.615),(0.52,0.60),(0.60,0.58),
     (0.70,0.585),(0.80,0.55),(0.90,0.505),(1.0,0.47)]
def land_top(xf):
    for i in range(len(_lt)-1):
        x0,y0=_lt[i]; x1,y1=_lt[i+1]
        if xf<=x1:
            t=(xf-x0)/(x1-x0) if x1>x0 else 0.0
            return H*(y0+(y1-y0)*max(0.0,min(1.0,t)))
    return H*_lt[-1][1]

HAZE=(198,211,214)
def hz(c, f=0.58): return tuple(int(round(c[k]*(1-f)+HAZE[k]*f)) for k in range(3))
def sky_ell(cx, cy, rx, ry, a0, a1): rgba_ell("rgba(%d,%d,%d,1.0)"%HAZE, cx, cy, rx, ry, a0, a1)

def bg_col_temple(cx, base, wd, h):
    c=hz((230,226,212)); cs=hz((196,190,172)); rf=hz((214,208,190))
    rect(cs, cx-wd*0.58, base-h*0.10, cx+wd*0.58, base)
    for i in range(6):
        x=cx-wd*0.46+(wd*0.92)*i/5
        rect(c, x-wd*0.045, base-h*0.66, x+wd*0.045, base-h*0.10)
        rect(cs, x+wd*0.012, base-h*0.66, x+wd*0.045, base-h*0.10)
    rect(cs, cx-wd*0.52, base-h*0.78, cx+wd*0.52, base-h*0.66)
    poly(rf, [(cx-wd*0.56, base-h*0.78),(cx+wd*0.56, base-h*0.78),(cx, base-h*0.98)])
    poly(hz((196,190,172)), [(cx, base-h*0.78),(cx+wd*0.56, base-h*0.78),(cx, base-h*0.98)])

def bg_aqueduct(x0, x1, base, h):
    c=hz((216,206,185)); cs=hz((190,180,158))
    n=max(3,int((x1-x0)/(H*0.030)))
    seg=(x1-x0)/n
    rect(c, x0, base-h, x1, base)
    for i in range(n):
        px=x0+seg*(i+0.5)
        sky_ell(px, base, seg*0.34, h*0.66, 180, 360)
    rect(cs, x0, base-h, x1, base-h*0.86)
    rect(c, x0-seg*0.1, base-h*1.02, x1+seg*0.1, base-h*0.86)

def bg_rotunda(cx, base, wd, h):
    c=hz((228,222,206)); cs=hz((198,192,174)); dm=hz((216,208,190))
    rect(cs, cx-wd*0.5, base-h*0.46, cx+wd*0.5, base)
    sky_ell(cx, base-h*0.46, wd*0.5, h*0.46, 180, 360)
    for i in range(4):
        x=cx-wd*0.34+wd*0.68*i/3
        rect(c, x-wd*0.05, base-h*0.46, x+wd*0.05, base)
    poly(dm, [(cx-wd*0.46, base-h*0.46),(cx+wd*0.46, base-h*0.46),(cx, base-h*0.66)])

def bg_arch(cx, base, wd, h):
    c=hz((220,210,188)); cs=hz((192,182,160))
    rect(c, cx-wd*0.5, base-h, cx+wd*0.5, base)
    sky_ell(cx, base, wd*0.24, h*0.52, 180, 360)
    rect(cs, cx-wd*0.6, base-h*1.06, cx+wd*0.6, base-h*0.88)

def bg_column(cx, base, h):
    c=hz((226,220,204)); cs=hz((196,190,172))
    rect(c, cx-h*0.045, base-h, cx+h*0.045, base)
    rect(cs, cx+h*0.012, base-h, cx+h*0.045, base)
    rect(cs, cx-h*0.075, base-h*1.08, cx+h*0.075, base-h)
    rect(c, cx-h*0.09, base-h*0.02, cx+h*0.09, base+h*0.04)

def bg_tholos(cx, base, wd, h):
    c=hz((230,224,208)); cs=hz((198,192,174)); rf=hz((214,206,188))
    for i in range(6):
        x=cx-wd*0.4+wd*0.8*i/5
        rect(c, x-wd*0.04, base-h*0.6, x+wd*0.04, base)
    sky_ell(cx, base-h*0.6, wd*0.5, h*0.36, 180, 360)
    rect(cs, cx-wd*0.46, base-h*0.6, cx+wd*0.46, base-h*0.52)
    poly(rf, [(cx-wd*0.48, base-h*0.6),(cx+wd*0.48, base-h*0.6),(cx, base-h*0.82)])

bg_col_temple(W*0.470, land_top(0.470)+H*0.050, H*0.072, H*0.120)
bg_aqueduct(W*0.548, W*0.636, land_top(0.59)+H*0.048, H*0.082)
bg_rotunda(W*0.680, land_top(0.680)+H*0.044, H*0.078, H*0.112)
bg_arch(W*0.775, land_top(0.775)+H*0.040, H*0.052, H*0.082)
bg_column(W*0.900, land_top(0.900)+H*0.030, H*0.120)
bg_tholos(W*0.955, land_top(0.955)+H*0.028, H*0.058, H*0.095)

# a sandy desert patch under the pyramids
poly((236,214,160), [(W*0.36, WL), (W*0.585, WL), (W*0.55, H*0.63), (W*0.40, H*0.645)])

# ---- wonders (left -> right along the coast) ----
def pyramid(cx, by, hw, hh):
    gshadow(cx+hw*0.35, by, hw*1.2, hh*0.05, 0.20)
    litT=(248,228,180); litB=(228,200,142); shdT=(212,186,134); shdB=(184,156,104)
    apex=(cx, by-hh)
    tri_band(apex,(cx-hw,by),(cx,by), litT, litB, 9)
    tri_band(apex,(cx,by),(cx+hw,by), shdT, shdB, 9)
    poly(lt(litT,1.05), [apex,(cx-hw*0.11,by-hh*0.86),(cx+hw*0.11,by-hh*0.86)])
    line(lt(litT,1.08), max(0.8,1.0*S), apex, (cx, by))
    for k in (0.32,0.55,0.78):
        y=by-hh*k
        line(dk(litB,0.9), max(0.6,0.7*S), (cx-hw*(1-k), y), (cx, y))
        line(dk(shdB,0.9), max(0.6,0.7*S), (cx, y), (cx+hw*(1-k), y))

def palm(bx, by, h):
    line((120,86,52), max(1.4,2.0*S), (bx, by), (bx-h*0.06, by-h))
    top=(bx-h*0.06, by-h)
    for a in (-0.9,-0.4,0.1,0.6,1.1):
        ex=top[0]+math.cos(a)*h*0.5; ey=top[1]-abs(math.sin(a+0.4))*h*0.10 - h*0.02 + (a)*h*0.06
        poly((70,150,80), [top, (top[0]+math.cos(a-0.12)*h*0.5, ey), (ex+math.cos(a)*h*0.04, ey+h*0.05)])

pyramid(W*0.470, WL, H*0.085, H*0.150)
pyramid(W*0.420, WL, H*0.058, H*0.098)
pyramid(W*0.520, WL, H*0.045, H*0.078)
palm(W*0.392, WL, H*0.115)

def temple(cx, by, wd, ht):
    marble=(242,238,226); shade=(210,204,186); dark=(150,146,132)
    roof=(230,224,206); cap=(232,226,208)
    gshadow(cx, by, wd*0.64, ht*0.05, 0.20)
    rect(dk(marble,0.86), cx-wd*0.60, by-ht*0.05, cx+wd*0.60, by)
    rect(shade, cx-wd*0.56, by-ht*0.10, cx+wd*0.56, by-ht*0.05)
    rect(marble, cx-wd*0.52, by-ht*0.14, cx+wd*0.52, by-ht*0.10)
    n=7
    cw=wd*0.036
    for i in range(n):
        x=cx-wd*0.46 + (wd*0.92)*i/(n-1)
        rect(marble, x-cw, by-ht*0.70, x+cw, by-ht*0.14)
        rect(shade, x+cw*0.30, by-ht*0.70, x+cw, by-ht*0.14)
        for f in (-0.55,0.0,0.55):
            line(dk(marble,0.88), max(0.5,0.6*S), (x+cw*f, by-ht*0.68), (x+cw*f, by-ht*0.16))
        rect(cap, x-cw*1.3, by-ht*0.74, x+cw*1.3, by-ht*0.70)
    rect(marble, cx-wd*0.54, by-ht*0.80, cx+wd*0.54, by-ht*0.74)
    rect(shade, cx-wd*0.54, by-ht*0.84, cx+wd*0.54, by-ht*0.80)
    for i in range(9):
        gx=cx-wd*0.48+wd*0.96*i/8
        rect(dark, gx-wd*0.012, by-ht*0.836, gx+wd*0.012, by-ht*0.804)
    poly(roof, [(cx-wd*0.58, by-ht*0.84), (cx+wd*0.58, by-ht*0.84), (cx, by-ht*1.14)])
    poly(dk(roof,0.9), [(cx, by-ht*0.84), (cx+wd*0.58, by-ht*0.84), (cx, by-ht*1.14)])
    line(dk(roof,0.8), max(0.6,0.8*S), (cx-wd*0.58, by-ht*0.84), (cx+wd*0.58, by-ht*0.84))
    for ax,ay in [(cx, by-ht*1.14),(cx-wd*0.58, by-ht*0.84),(cx+wd*0.58, by-ht*0.84)]:
        ell(cap, ax, ay-ht*0.02, wd*0.02, ht*0.028)

temple(W*0.560, H*0.62, H*0.150, H*0.205)

def eiffel(cx, by, ht):
    iron=(122,96,66); irl=(154,128,92); ird=(92,72,50); irh=(182,156,118)
    y1=by-ht*0.30; y2=by-ht*0.60; y3=by-ht*0.90; y4=by-ht
    w0=ht*0.185; w1=ht*0.095; w2=ht*0.048; w3=ht*0.020
    gshadow(cx, by, w0*1.3, ht*0.03, 0.20)
    def lattice(ya,wa,yb,wb,rungs):
        for r in range(rungs):
            s0=r/rungs; s1=(r+1)/rungs
            ya0=ya+(yb-ya)*s0; ya1=ya+(yb-ya)*s1
            wla=wa+(wb-wa)*s0; wlb=wa+(wb-wa)*s1
            line(irl, max(0.5,0.6*S), (cx-wla, ya0), (cx+wlb, ya1))
            line(irl, max(0.5,0.6*S), (cx+wla, ya0), (cx-wlb, ya1))
            line(ird, max(0.4,0.5*S), (cx-wlb, ya1), (cx+wlb, ya1))
    poly(iron, [(cx-w0, by), (cx-w0*0.55, y1), (cx-w1, y1), (cx-w0*0.55, by)])
    poly(iron, [(cx+w0, by), (cx+w0*0.55, y1), (cx+w1, y1), (cx+w0*0.55, by)])
    for sgn in (-1,1):
        for r in range(3):
            s0=r/3.0; s1=(r+1)/3.0
            ox0=cx+sgn*(w0-(w0-w0*0.55)*s0); ox1=cx+sgn*(w0-(w0-w0*0.55)*s1)
            ix0=cx+sgn*(w0*0.62-(w0*0.62-w1)*s0); ix1=cx+sgn*(w0*0.62-(w0*0.62-w1)*s1)
            line(irl, max(0.5,0.6*S), (ox0, by+(y1-by)*s0), (ix1, by+(y1-by)*s1))
    out.append("fill none stroke %s stroke-width %.2f path 'M %.2f,%.2f Q %.2f,%.2f %.2f,%.2f'" % (
        hx(iron), max(2.0,3.0*S), cx-w0*0.80, by-ht*0.02, cx, by-ht*0.16, cx+w0*0.80, by-ht*0.02))
    out.append("stroke none")
    rect(ird, cx-w1*1.15, y1-ht*0.02, cx+w1*1.15, y1+ht*0.01)
    poly(iron, [(cx-w1, y1), (cx-w2, y2), (cx+w2, y2), (cx+w1, y1)])
    lattice(y1, w1, y2, w2, 5)
    rect(ird, cx-w2*1.25, y2-ht*0.015, cx+w2*1.25, y2+ht*0.008)
    poly(iron, [(cx-w2, y2), (cx-w3, y3), (cx+w3, y3), (cx+w2, y2)])
    lattice(y2, w2, y3, w3, 6)
    poly(iron, [(cx-w3, y3), (cx-ht*0.006, y4), (cx+ht*0.006, y4), (cx+w3, y3)])
    line(ird, max(1.0,1.4*S), (cx, y3), (cx, y4))
    ell(iron, cx, y4-ht*0.008, ht*0.011, ht*0.014)
    line(irh, max(0.6,0.8*S), (cx-w0, by), (cx-w1, y1))
    line(irh, max(0.6,0.8*S), (cx-w1, y1), (cx-w2, y2))
    line(irh, max(0.5,0.7*S), (cx-w2, y2), (cx-w3, y3))

eiffel(W*0.640, WL, H*0.44)

def colosseum(cx, by, wd, ht):
    litc=(238,224,188); stone=(220,204,164); shd=(190,172,132); dark=(112,96,72); grass=(150,182,120)
    gshadow(cx, by, wd*1.15, ht*0.06, 0.20)
    top=by-ht
    ell(stone, cx, top+ht*0.16, wd, ht*0.16)
    ell(grass, cx, top+ht*0.15, wd*0.62, ht*0.10)
    rect(stone, cx-wd, top+ht*0.16, cx+wd, by)
    poly(litc, [(cx-wd, top+ht*0.16),(cx-wd*0.46, top+ht*0.16),(cx-wd*0.46, by),(cx-wd,by)])
    poly(shd, [(cx+wd, top+ht*0.16),(cx+wd*0.5, top+ht*0.16),(cx+wd*0.5, by),(cx+wd,by)])
    ell(shd, cx, by, wd, ht*0.13)
    ell(stone, cx, by-ht*0.05, wd*0.99, ht*0.11)
    na=9
    for i in range(na):
        t=i/(na-1.0)
        ax=cx-wd*0.9+wd*1.8*t
        aw=wd*0.045*(1-abs(t-0.5)*0.5)
        rect(dark, ax-aw, top+ht*0.19, ax+aw, top+ht*0.29)
    for ya,yb in [(top+ht*0.34, top+ht*0.55),(top+ht*0.60, top+ht*0.81)]:
        for i in range(na):
            t=i/(na-1.0)
            ax=cx-wd*0.9+wd*1.8*t
            aw=wd*0.05*(1-abs(t-0.5)*0.45)
            if aw<=0.3*S: continue
            rect(dark, ax-aw, ya+(yb-ya)*0.4, ax+aw, yb)
            rgba_ell("rgba(112,96,72,1.0)", ax, ya+(yb-ya)*0.4, aw, (yb-ya)*0.4, 180, 360)
    line(dk(stone,0.9), max(0.5,0.6*S), (cx-wd, top+ht*0.32), (cx+wd, top+ht*0.32))
    line(dk(stone,0.9), max(0.5,0.6*S), (cx-wd, top+ht*0.575), (cx+wd, top+ht*0.575))

colosseum(W*0.720, WL, H*0.056, H*0.155)

def pagoda(cx, by, wd, ht):
    red=(198,74,60); rdk=(150,52,44); roof=(120,66,58); gold=(240,200,90); wood=(150,96,60)
    gshadow(cx, by, wd*0.62, ht*0.05, 0.20)
    tiers=[(0.0,0.30,1.00),(0.28,0.26,0.80),(0.52,0.22,0.62),(0.72,0.0,0.42)]
    for i,(yb,bh,rw) in enumerate(tiers):
        y0=by-ht*yb
        if bh>0:
            rect(red, cx-wd*rw*0.5, y0-ht*bh, cx+wd*rw*0.5, y0)
            rect(rdk, cx+wd*rw*0.18, y0-ht*bh, cx+wd*rw*0.5, y0)
            rect(wood, cx-wd*rw*0.08, y0-ht*bh, cx+wd*rw*0.08, y0)
        ry=y0-ht*bh; rw2=wd*rw*0.84
        out.append("fill %s stroke none path 'M %.2f,%.2f Q %.2f,%.2f %.2f,%.2f Q %.2f,%.2f %.2f,%.2f Q %.2f,%.2f %.2f,%.2f Z'" % (
            hx(roof), cx-rw2, ry-ht*0.045, cx-rw2*0.42, ry-ht*0.135, cx, ry-ht*0.105,
            cx+rw2*0.42, ry-ht*0.135, cx+rw2, ry-ht*0.045,
            cx, ry+ht*0.055, cx-rw2, ry-ht*0.045))
        line(gold, max(0.8,1.0*S), (cx-rw2, ry-ht*0.045), (cx, ry-ht*0.10))
        line(gold, max(0.8,1.0*S), (cx, ry-ht*0.10), (cx+rw2, ry-ht*0.045))
    line(gold, max(1.4,2.0*S), (cx, by-ht*1.02), (cx, by-ht*1.16))
    ell(gold, cx, by-ht*1.16, wd*0.05, wd*0.05)

pagoda(W*0.820, H*0.585, H*0.115, H*0.185)

# a great-wall segment marching over the far ridge on the right
def great_wall():
    stone=(196,180,150); shd=(168,152,124); top=(214,200,172)
    pts=[(W*0.86, H*0.505),(W*0.90, H*0.470),(W*0.935, H*0.500),(W*0.965, H*0.455),(W, H*0.478)]
    for i in range(len(pts)-1):
        a,b=pts[i],pts[i+1]
        dx,dy=b[0]-a[0],b[1]-a[1]; L=math.hypot(dx,dy) or 1
        nx,ny=-dy/L,dx/L; th=H*0.055
        poly(stone, [(a[0],a[1]),(b[0],b[1]),(b[0]+nx*th,b[1]+ny*th),(a[0]+nx*th,a[1]+ny*th)])
        line(top, max(1.4,2.0*S), a, b)
        m=int(L/(10*S))+1
        for k in range(m):
            t=k/max(1,m-1)
            mx=a[0]+dx*t; my=a[1]+dy*t
            rect(top, mx-2*S, my-4*S, mx+2*S, my)
    for tx,ty in [(W*0.90, H*0.470),(W*0.965, H*0.455)]:
        rect(stone, tx-4*S, ty-H*0.05, tx+4*S, ty+H*0.02)
        rect(shd, tx+1*S, ty-H*0.05, tx+4*S, ty+H*0.02)
        rect(top, tx-5*S, ty-H*0.06, tx+5*S, ty-H*0.05)

great_wall()

# ---- shore road with cars, running along the waterfront ----
road_y = WL - H*0.028
poly((92,94,104), [(W*0.30, WL+H*0.004), (W, WL-H*0.02), (W, road_y-H*0.030), (W*0.315, road_y-H*0.010)])
for i in range(11):
    t=i/10.0
    x=W*0.34+ (W*0.62)*t
    y=road_y-H*0.016 - t*H*0.006
    rect((236,214,120), x-6*S, y-1.4*S, x+6*S, y+1.4*S)

def car(cx, by, L, col, kind="coupe"):
    dark=(40,44,54); glass=(160,202,228); tire=(38,38,44); hub=(150,150,158)
    h=L*0.42
    rgba_ell("rgba(28,32,38,0.24)", cx, by+L*0.02, L*0.56, L*0.11)
    if kind=="bus":
        h=L*0.62
        rect(dk(col,0.9), cx-L*0.5, by-h, cx+L*0.5, by-L*0.14)
        rect(col, cx-L*0.5, by-h, cx+L*0.5, by-h*0.5)
        for i in range(4):
            x=cx-L*0.4+ i*L*0.24
            rect(glass, x, by-h*0.86, x+L*0.14, by-h*0.55)
    else:
        rect(col, cx-L*0.5, by-h*0.55, cx+L*0.5, by-L*0.14)
        out.append("fill %s stroke none path 'M %.2f,%.2f Q %.2f,%.2f %.2f,%.2f L %.2f,%.2f Q %.2f,%.2f %.2f,%.2f Z'" % (
            hx(col), cx-L*0.24, by-h*0.55, cx-L*0.16, by-h, cx+L*0.06, by-h,
            cx+L*0.30, by-h, cx+L*0.30, by-h*0.55, cx-L*0.24, by-h*0.55))
        rect(glass, cx-L*0.18, by-h*0.92, cx+L*0.02, by-h*0.6)
        rect(glass, cx+L*0.05, by-h*0.92, cx+L*0.26, by-h*0.6)
        line(lt(col,1.22), max(0.6,0.8*S), (cx-L*0.44, by-h*0.5), (cx+L*0.28, by-h*0.5))
        rect(dk(col,0.86), cx-L*0.5, by-L*0.2, cx+L*0.5, by-L*0.14)
        rect((252,236,156), cx+L*0.46, by-h*0.42, cx+L*0.5, by-h*0.24)
        ell((220,70,60), cx-L*0.49, by-L*0.28, L*0.03, L*0.05)
    for wx in (cx-L*0.30, cx+L*0.30):
        ell(tire, wx, by-L*0.10, L*0.14, L*0.14)
        ell(hub, wx, by-L*0.10, L*0.06, L*0.06)
        ell(lt(hub,1.2), wx, by-L*0.10, L*0.025, L*0.025)

car(W*0.415, road_y-H*0.012, H*0.052, (216,72,66), "coupe")
car(W*0.560, road_y-H*0.020, H*0.048, (244,196,72), "coupe")
car(W*0.720, road_y-H*0.028, H*0.058, (74,150,196), "bus")
car(W*0.870, road_y-H*0.036, H*0.050, (90,182,120), "coupe")

# ---- the sea ----
poly((60,132,182), [(W*0.285, WL), (W, WL), (W, H), (-30*S, H), (-30*S, WL+H*0.03)])
rgba_poly("rgba(150,198,224,0.55)", [(-30*S, WL+H*0.03), (W, WL), (W, WL+H*0.03), (-30*S, WL+H*0.06)])
random.seed(51)
for _ in range(26):
    sx=random.uniform(0, W); sy=random.uniform(WL+H*0.05, H-4*S)
    sw=random.uniform(14,40)*S*(0.5+(sy-WL)/(H-WL))
    rgba_ell("rgba(210,234,246,0.35)", sx, sy, sw, max(1.0,1.2*S))

def galleon(cx, wl, s):
    hull=(120,80,48); hdk=(92,60,36); sail=(246,242,232); sdk=(216,210,194); flag=(212,72,66); rig=(74,54,38)
    rgba_ell("rgba(40,36,30,0.16)", cx+s*0.1, wl+s*0.5, s*1.2, s*0.7)
    for i in range(3):
        rgba_ell("rgba(238,232,214,%.2f)"%(0.20-i*0.05), cx+(i-1)*s*0.5, wl+s*(0.62+i*0.34), s*(0.7-i*0.16), max(0.9,1.0*S))
    poly(hull, [(cx-s*1.3, wl), (cx+s*1.35, wl), (cx+s*1.0, wl+s*0.42), (cx-s*0.95, wl+s*0.42)])
    poly(hdk, [(cx-s*1.3, wl), (cx+s*1.35, wl), (cx+s*1.2, wl+s*0.16), (cx-s*1.15, wl+s*0.16)])
    poly((238,226,198), [(cx-s*1.3, wl-s*0.24), (cx+s*1.35, wl-s*0.24), (cx+s*1.35, wl), (cx-s*1.3, wl)])
    line((216,182,112), max(0.6,0.8*S), (cx-s*1.28, wl-s*0.12), (cx+s*1.33, wl-s*0.12))
    line(rig, max(1.0,1.2*S), (cx+s*1.18, wl-s*0.18), (cx+s*1.85, wl-s*0.52))
    tops=[]
    for mx,mh in [(cx-s*0.6, s*1.9),(cx+s*0.15, s*2.3),(cx+s*0.82, s*1.7)]:
        line(rig, max(1.2,1.6*S), (mx, wl-s*0.24), (mx, wl-s*0.24-mh))
        top=wl-s*0.24-mh; tops.append((mx,top))
        poly(sail, [(mx-s*0.5, top+mh*0.18), (mx+s*0.5, top+mh*0.18), (mx+s*0.42, top+mh*0.52), (mx-s*0.42, top+mh*0.52)])
        poly(sdk, [(mx-s*0.44, top+mh*0.56), (mx+s*0.44, top+mh*0.56), (mx+s*0.34, top+mh*0.88), (mx-s*0.34, top+mh*0.88)])
        line(dk(sdk,0.9), max(0.4,0.5*S), (mx, top+mh*0.18), (mx, top+mh*0.88))
        poly(flag, [(mx, top), (mx+s*0.42, top+s*0.10), (mx, top+s*0.20)])
    line(rig, max(0.4,0.5*S), (cx+s*1.85, wl-s*0.52), tops[2])
    line(rig, max(0.4,0.5*S), tops[0], (cx-s*1.12, wl-s*0.08))

def steamer(cx, wl, s):
    hull=(58,72,96); hdk=(40,52,72); cabin=(240,238,232); csh=(210,208,202); stack=(196,80,64); dark=(52,54,62); gold=(224,190,120)
    rgba_ell("rgba(40,44,56,0.16)", cx, wl+s*0.6, s*1.5, s*0.7)
    for i in range(3):
        rgba_ell("rgba(214,152,124,%.2f)"%(0.16-i*0.04), cx+(i-1)*s*0.4, wl+s*(0.7+i*0.3), s*(0.5-i*0.12), max(0.9,1.0*S))
    poly(hull, [(cx-s*1.7, wl), (cx+s*1.7, wl), (cx+s*1.3, wl+s*0.5), (cx-s*1.5, wl+s*0.5)])
    poly(hdk, [(cx-s*1.7, wl), (cx+s*1.7, wl), (cx+s*1.55, wl+s*0.18), (cx-s*1.6, wl+s*0.18)])
    line(gold, max(0.5,0.7*S), (cx-s*1.64, wl-s*0.02), (cx+s*1.64, wl-s*0.02))
    rect(cabin, cx-s*1.0, wl-s*0.7, cx+s*0.9, wl)
    rect(csh, cx+s*0.2, wl-s*0.7, cx+s*0.9, wl)
    for i in range(5):
        ell(dark, cx-s*0.8+ i*s*0.4, wl-s*0.4, s*0.075, s*0.075)
    rect(stack, cx-s*0.15, wl-s*1.5, cx+s*0.28, wl-s*0.7)
    rect(dark, cx-s*0.15, wl-s*1.5, cx+s*0.28, wl-s*1.4)
    line((60,44,34), max(1.0,1.2*S), (cx+s*1.48, wl-s*0.02), (cx+s*1.48, wl-s*1.0))
    poly((70,150,210), [(cx+s*1.48, wl-s*1.0),(cx+s*1.98, wl-s*0.9),(cx+s*1.48, wl-s*0.78)])
    for i in range(5):
        rgba_ell("rgba(120,124,134,%.2f)" % (0.5-i*0.08), cx+s*0.05+i*s*0.28, wl-s*1.7-i*s*0.5, s*(0.3+i*0.14), s*(0.26+i*0.12))

def sailboat(cx, wl, s):
    hull=(120,80,48); sail=(246,242,232)
    rgba_ell("rgba(40,36,30,0.14)", cx, wl+s*0.42, s*0.8, s*0.4)
    for i in range(2):
        rgba_ell("rgba(238,232,214,%.2f)"%(0.16-i*0.06), cx, wl+s*(0.55+i*0.3), s*(0.4-i*0.12), max(0.8,0.9*S))
    poly(hull, [(cx-s*0.8, wl), (cx+s*0.8, wl), (cx+s*0.55, wl+s*0.34), (cx-s*0.55, wl+s*0.34)])
    line((70,50,34), max(1.0,1.2*S), (cx, wl), (cx, wl-s*1.5))
    poly(sail, [(cx+s*0.06, wl-s*1.45), (cx+s*0.06, wl-s*0.1), (cx+s*0.7, wl-s*0.1)])
    poly(dk(sail,0.92), [(cx-s*0.06, wl-s*1.2), (cx-s*0.06, wl-s*0.1), (cx-s*0.55, wl-s*0.1)])

galleon(W*0.470, WL+H*0.085, H*0.062)
steamer(W*0.700, WL+H*0.150, H*0.052)
sailboat(W*0.230, WL+H*0.120, H*0.050)
sailboat(W*0.880, WL+H*0.210, H*0.044)

sys.stdout.write(" ".join(out))
PY
)
convert "$w/sky6.png" -draw "$scene" "$w/scene0.png"

# a soft light wash on the left keeps the wordmark legible over the panorama
convert -size ${H}x${W} gradient:black-white -rotate 90 \
    -evaluate pow 2.0 -evaluate multiply 0.46 "$w/lmask.png"
convert -size ${W}x${H} xc:'#f8f2e4' "$w/lmask.png" \
    -alpha off -compose CopyOpacity -composite "$w/lwash.png"
convert "$w/scene0.png" "$w/lwash.png" -compose over -composite "$w/scene.png"

# text labels — rendered at 2x, downscale crisp; over the open left sky
P() { echo $(( $1 * S )); }
convert -background none -font "$fr" -pointsize $(P 54) -kerning $((1*S)) -fill '#28344f' label:'Nordstjernen ' "$w/t1.png"
convert -background none -font "$fr" -pointsize $(P 54) -fill '#b96a12' label:"$ver" "$w/t2.png"
convert -background none -font "$fr" -pointsize $(P 25) -fill '#295169' label:'Nordstjernen Web Browser' "$w/ts.png"
convert -background none -font "$fr" -pointsize $(P 23) -kerning $((3*S)) -fill '#b96a12' label:"$codename" "$w/tc.png"
convert -background none -font "$fr" -pointsize $(P 20) -fill '#2c3f54' \
    label:'Étoile du Nord — the legendary web browser' "$w/t3.png"

# a soft light plate behind each line keeps it legible over the scene;
# the lower lines cross the busier foreground, so their plate is denser
for n in t1 t2; do
    convert "$w/$n.png" -channel A -blur 0x$((4*S)) -level 0,60% +channel \
        -fill '#f6f1e4' -colorize 100 -channel A -evaluate multiply 0.55 +channel "$w/${n}g.png"
done
for n in ts tc t3; do
    convert "$w/$n.png" -channel A -blur 0x$((4*S)) -level 0,42% +channel \
        -fill '#f8f3e7' -colorize 100 "$w/${n}g.png"
done

w1=$(identify -format '%w' "$w/t1.png"); h1=$(identify -format '%h' "$w/t1.png")
hs=$(identify -format '%h' "$w/ts.png"); hc=$(identify -format '%h' "$w/tc.png")
g1=$((14*S)); g2=$((14*S)); g3=$((12*S))
ty=$((46*S)); textleft=$((80*S))
sy=$((ty + h1 + g1)); cy=$((sy + hs + g2)); gy=$((cy + hc + g3))

convert "$w/scene.png" \
    "$w/t1g.png" -gravity NorthWest -geometry +${textleft}+${ty} -compose over -composite \
    "$w/t2g.png" -gravity NorthWest -geometry +$((textleft + w1))+${ty} -compose over -composite \
    "$w/tsg.png" -gravity NorthWest -geometry +${textleft}+${sy} -compose over -composite \
    "$w/tcg.png" -gravity NorthWest -geometry +${textleft}+${cy} -compose over -composite \
    "$w/t3g.png" -gravity NorthWest -geometry +${textleft}+${gy} -compose over -composite \
    "$w/t3g.png" -gravity NorthWest -geometry +${textleft}+${gy} -compose over -composite \
    "$w/t1.png" -gravity NorthWest -geometry +${textleft}+${ty} -compose over -composite \
    "$w/t2.png" -gravity NorthWest -geometry +$((textleft + w1))+${ty} -compose over -composite \
    "$w/ts.png" -gravity NorthWest -geometry +${textleft}+${sy} -compose over -composite \
    "$w/tc.png" -gravity NorthWest -geometry +${textleft}+${cy} -compose over -composite \
    "$w/t3.png" -gravity NorthWest -geometry +${textleft}+${gy} -compose over -composite \
    "$w/full2x.png"

# single high-quality downscale to the final size
convert "$w/full2x.png" -filter Lanczos -resize 940x320 -strip PNG24:"$w/splash.png"
echo "rendered splash 940x320 (2x supersampled) for $ver $codename ($(stat -c%s "$w/splash.png") bytes)"

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
