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

# summer sky: blue to warm pale at the horizon, with a soft sun glow
convert -size ${W}x${H} gradient:'#4ea7e6'-'#e3f4f7' "$w/sky.png"
convert -size ${W}x${H} gradient:none-'#fcf3c6' "$w/warm.png"
convert "$w/sky.png" "$w/warm.png" -compose over -composite "$w/sky1.png"
convert -size ${W}x${H} xc:none -fill '#fff3c8' \
    -draw "ellipse $((800*S)),$((72*S)) $((250*S)),$((170*S)) 0,360" -blur 0x$((80*S)) "$w/glow.png"
convert "$w/sky1.png" "$w/glow.png" -compose screen -composite "$w/sky2.png"

# soft clouds
convert "$w/sky2.png" -fill 'rgba(255,255,255,0.85)' \
    -draw "ellipse $((250*S)),$((70*S)) $((46*S)),$((16*S)) 0,360 ellipse $((300*S)),$((76*S)) $((40*S)),$((18*S)) 0,360 ellipse $((210*S)),$((80*S)) $((30*S)),$((12*S)) 0,360" \
    -draw "ellipse $((560*S)),$((52*S)) $((38*S)),$((13*S)) 0,360 ellipse $((600*S)),$((58*S)) $((30*S)),$((12*S)) 0,360" \
    -draw "ellipse $((430*S)),$((44*S)) $((34*S)),$((12*S)) 0,360 ellipse $((466*S)),$((50*S)) $((26*S)),$((10*S)) 0,360" \
    -blur 0x$((2*S)) "$w/sky3.png"

# the sun: warm disc + corona + rays
python3 - "$S" > "$w/sun.txt" <<'PY'
import math, sys
S = float(sys.argv[1])
cx, cy, R = 800*S, 72*S, 40*S
out = []
for k in range(20):
    a = 2*math.pi*k/20
    r0, r1, wdt = R*1.32, R*2.25, 0.10
    p1 = (cx+r0*math.cos(a-wdt), cy+r0*math.sin(a-wdt))
    p2 = (cx+r1*math.cos(a), cy+r1*math.sin(a))
    p3 = (cx+r0*math.cos(a+wdt), cy+r0*math.sin(a+wdt))
    out.append("fill #ffd84a stroke none polygon %.2f,%.2f %.2f,%.2f %.2f,%.2f" % (p1[0],p1[1],p2[0],p2[1],p3[0],p3[1]))
print(" ".join(out))
PY
rays=$(cat "$w/sun.txt")
convert -size ${W}x${H} xc:none -draw "$rays" -blur 0x$((1*S)) "$w/rays.png"
convert -size ${W}x${H} xc:none -fill '#ffdf6a' \
    -draw "circle $((800*S)),$((72*S)) $((800*S)),$((112*S))" -blur 0x$((9*S)) "$w/scorona.png"
convert "$w/sky3.png" "$w/rays.png" -compose screen -composite \
        "$w/scorona.png" -compose screen -composite "$w/sky4.png"
convert "$w/sky4.png" \
    -fill '#ffd23e' -draw "circle $((800*S)),$((72*S)) $((800*S)),$((108*S))" \
    -fill '#ffe277' -draw "circle $((792*S)),$((64*S)) $((792*S)),$((86*S))" \
    -fill '#fff2b8' -draw "circle $((788*S)),$((60*S)) $((788*S)),$((72*S))" "$w/sky5.png"

# sky decorations: hot-air balloons, kites, a V of birds
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
def ell(col, cx, cy, rx, ry):
    out.append("fill %s stroke none ellipse %.2f,%.2f %.2f,%.2f 0,360" % (hx(col), cx, cy, rx, ry))
def line(col, wid, a, b):
    out.append("stroke %s stroke-width %.2f stroke-linecap round line %.2f,%.2f %.2f,%.2f" % (hx(col), wid, a[0], a[1], b[0], b[1]))
    out.append("stroke none")
def bird(cx, cy, s):
    w = s
    line((58,72,92), max(1.0,1.6*S), (cx-w, cy+w*0.34), (cx, cy))
    line((58,72,92), max(1.0,1.6*S), (cx, cy), (cx+w, cy+w*0.34))

def balloon(cx, cy, r, c1, c2):
    poly(dk(c1,0.9), [(cx-r*0.55, cy+r*0.55), (cx+r*0.55, cy+r*0.55), (cx+r*0.16, cy+r*1.05), (cx-r*0.16, cy+r*1.05)])
    ell(c1, cx, cy, r, r*1.12)
    ell(c2, cx-r*0.33, cy, r*0.34, r*1.12)
    ell(c2, cx+r*0.33, cy, r*0.34, r*1.12)
    ell(lt(c1,1.18), cx-r*0.30, cy-r*0.45, r*0.22, r*0.30)
    bw = r*0.28
    line((90,64,40), max(1.0,1.2*S), (cx-r*0.32, cy+r*1.05), (cx-bw*0.6, cy+r*1.45))
    line((90,64,40), max(1.0,1.2*S), (cx+r*0.32, cy+r*1.05), (cx+bw*0.6, cy+r*1.45))
    poly((120,84,48), [(cx-bw*0.6, cy+r*1.45), (cx+bw*0.6, cy+r*1.45), (cx+bw*0.5, cy+r*1.62), (cx-bw*0.5, cy+r*1.62)])

balloon(W*0.735, H*0.235, H*0.070, (228,86,86), (250,224,120))
balloon(W*0.815, H*0.345, H*0.050, (78,142,210), (244,244,248))

def kite(cx, cy, s, c1, c2, tail_dir):
    top=(cx, cy-s); rgt=(cx+s*0.7, cy); bot=(cx, cy+s*1.25); lft=(cx-s*0.7, cy)
    poly(c1, [top, rgt, (cx,cy)]); poly(dk(c1,0.85),[rgt,bot,(cx,cy)])
    poly(c2, [top, lft, (cx,cy)]); poly(dk(c2,0.85),[lft,bot,(cx,cy)])
    line((40,52,70), max(0.8,1.0*S), top, bot); line((40,52,70), max(0.8,1.0*S), lft, rgt)
    tx, ty = bot
    for i in range(6):
        nx = tx + tail_dir*(i+1)*s*0.16 + math.sin(i*1.3)*s*0.10
        ny = ty + (i+1)*s*0.20
        line((110,120,140), max(0.8,1.0*S), (tx,ty), (nx,ny))
        if i%2==0:
            ell(c2 if i%4 else c1, nx, ny, s*0.10, s*0.07)
        tx, ty = nx, ny

kite(W*0.665, H*0.110, H*0.050, (245,180,52), (228,74,74), +1)
kite(W*0.72, H*0.072, H*0.042, (92,190,98), (70,120,212), -1)

bx, by = W*0.62, H*0.085
for i in range(4):
    bird(bx + i*W*0.020, by + i*H*0.016, H*0.016)
    bird(bx - i*W*0.020, by + i*H*0.016, H*0.016)

sys.stdout.write(" ".join(out))
PY
)
convert "$w/sky5.png" -draw "$skydraw" "$w/sky6.png"

# land + crowd + trees + winding path + pond + picnic + flowers
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
def ell(col, cx, cy, rx, ry):
    out.append("fill %s stroke none ellipse %.2f,%.2f %.2f,%.2f 0,360" % (hx(col), cx, cy, rx, ry))
def line(col, wid, a, b):
    out.append("stroke %s stroke-width %.2f stroke-linecap round line %.2f,%.2f %.2f,%.2f" % (hx(col), wid, a[0], a[1], b[0], b[1]))
    out.append("stroke none")
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
    pts = [(-30*S, H), (-30*S, eb(-30*S))]
    x = -30*S
    while x < W + 30*S:
        y = eb(x) - random.uniform(amp*0.2, amp) * ascale(x)
        pts.append((x, y)); x += (58 + random.uniform(-14, 14))*S
    pts += [(W+30*S, eb(W+30*S)), (W+30*S, H)]
    out.append("fill %s stroke none polygon %s" % (hx(color), " ".join("%.2f,%.2f" % p for p in pts)))
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
        poly(dk(shirt,0.82), [(cx-h*0.12, sh_y), (cx+h*0.12, sh_y), (cx+h*0.205, fy+1*S), (cx-h*0.205, fy+1*S)])
        poly(shirt, [(cx-h*0.12, sh_y), (cx+h*0.12, sh_y), (cx+h*0.20, fy), (cx-h*0.20, fy)])
    else:
        lw = h*0.075
        poly(pants, [(cx-h*0.11, hip_y), (cx-h*0.11+2*lw, hip_y), (cx-h*0.11+1.6*lw, fy), (cx-h*0.11+0.2*lw, fy)])
        poly(pants, [(cx+h*0.11-2*lw, hip_y), (cx+h*0.11, hip_y), (cx+h*0.11-0.2*lw, fy), (cx+h*0.11-1.6*lw, fy)])
        poly(dk(shirt,0.86), [(cx-h*0.14, sh_y), (cx+h*0.14, sh_y), (cx+h*0.12, hip_y+h*0.02), (cx-h*0.12, hip_y+h*0.02)])
        poly(shirt, [(cx-h*0.14, sh_y), (cx+h*0.115, sh_y), (cx+h*0.10, hip_y+h*0.01), (cx-h*0.12, hip_y+h*0.01)])
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
    ell(lt(skin,1.08), cx-hr*0.28, head_cy-hr*0.10, hr*0.30, hr*0.34)

land(H*0.60, (150,200,112), H*0.10, 4, H*0.34)
land(H*0.70, (118,186,92),  H*0.11, 8, H*0.36)
mtop = land(H*0.78, (92,170,74), H*0.09, 15, H*0.40)

pcx, pcy = W*0.62, H*0.90
prx, pry = W*0.085, H*0.052
ell((70,150,120), pcx, pcy+1.6*S, prx*1.03, pry*1.05)
ell((86,176,210), pcx, pcy, prx, pry)
ell((150,206,228), pcx, pcy-pry*0.30, prx*0.86, pry*0.55)
ell((255,240,170), pcx+prx*0.42, pcy+pry*0.10, prx*0.20, pry*0.28)
for i in range(3):
    ww = prx*(0.55-0.12*i)
    yy = pcy+pry*(0.18+0.22*i)
    line((220,238,246), max(1.0, 1.4*S), (pcx-ww, yy), (pcx+ww*0.7, yy))

random.seed(7)
path_pts = []
n = 26
for i in range(n+1):
    t = i/n
    x = W*0.30 + (W*0.55)*t + math.sin(t*math.pi*2.1)*W*0.045
    y = H*0.995 - (H*0.30)*t + math.cos(t*math.pi*1.6)*H*0.012
    path_pts.append((x, y))
for i in range(len(path_pts)-1):
    t = i/(len(path_pts)-1)
    wPath = (4.0 + 16.0*t) * S
    quad((226,210,160), path_pts[i], path_pts[i+1], wPath*0.5, (4.0+16.0*((i+1)/(len(path_pts)-1)))*S*0.5)
for i in range(len(path_pts)-1):
    t = i/(len(path_pts)-1)
    wPath = (4.0 + 16.0*t) * S
    quad((240,228,186), path_pts[i], path_pts[i+1], wPath*0.28, (4.0+16.0*((i+1)/(len(path_pts)-1)))*S*0.28)

def tree_round(bx, by, h):
    trunk_w = h*0.10
    poly((110,78,48), [(bx-trunk_w*0.5, by), (bx+trunk_w*0.5, by),
                       (bx+trunk_w*0.35, by-h*0.45), (bx-trunk_w*0.35, by-h*0.45)])
    cy = by - h*0.62; r = h*0.42
    ell(dk((58,138,66),0.86), bx+r*0.18, cy+r*0.16, r*0.96, r*0.92)
    ell((72,158,78), bx, cy, r, r*0.95)
    ell((104,190,104), bx-r*0.32, cy-r*0.30, r*0.45, r*0.40)
def tree_blob(bx, by, h):
    trunk_w = h*0.10
    poly((104,72,44), [(bx-trunk_w*0.5, by), (bx+trunk_w*0.5, by),
                       (bx+trunk_w*0.32, by-h*0.40), (bx-trunk_w*0.32, by-h*0.40)])
    base = (66,150,72)
    blobs = [(-0.30,-0.55,0.34),(0.30,-0.52,0.32),(0.0,-0.78,0.36),(-0.05,-0.50,0.40)]
    for dx,dy,r in blobs:
        ell(dk(base,0.9), bx+dx*h+r*h*0.12, by+dy*h+r*h*0.10, r*h, r*h*0.94)
    for dx,dy,r in blobs:
        ell(base, bx+dx*h, by+dy*h, r*h, r*h*0.94)
    ell(lt(base,1.18), bx-0.18*h, by-0.78*h, 0.18*h, 0.16*h)
def tree_pine(bx, by, h):
    trunk_w = h*0.08
    poly((100,68,42), [(bx-trunk_w*0.5, by), (bx+trunk_w*0.5, by),
                       (bx+trunk_w*0.4, by-h*0.22), (bx-trunk_w*0.4, by-h*0.22)])
    base = (54,134,76)
    tiers = [(by-h*0.18, h*0.34, by-h*0.50),
             (by-h*0.42, h*0.27, by-h*0.72),
             (by-h*0.64, h*0.18, by-h*0.92)]
    for i,(yb, half, yt) in enumerate(tiers):
        col = lt(base, 1.0+0.06*i)
        poly(dk(col,0.88), [(bx-half+1.5*S, yb), (bx+half+1.5*S, yb), (bx+1.5*S, yt)])
        poly(col, [(bx-half, yb), (bx+half, yb), (bx, yt)])

tree_pine(W*0.405, mtop(W*0.405)+H*0.10, H*0.30)
tree_round(W*0.515, mtop(W*0.515)+H*0.06, H*0.25)
tree_blob(W*0.955, mtop(W*0.955)+H*0.05, H*0.34)
tree_round(W*0.835, mtop(W*0.835)+H*0.03, H*0.22)

def blanket(cx, cy, wdt, hgt, c1, c2):
    skew = wdt*0.32
    p = [(cx-wdt*0.5, cy), (cx-wdt*0.5+skew, cy-hgt), (cx+wdt*0.5+skew, cy-hgt), (cx+wdt*0.5, cy)]
    poly(dk(c1,0.85), [(p[0][0],p[0][1]+1.6*S),(p[1][0],p[1][1]+1.6*S),(p[2][0],p[2][1]+1.6*S),(p[3][0],p[3][1]+1.6*S)])
    poly(c1, p)
    for gx in range(-2,3):
        for gy in range(0,4):
            if (gx+gy)%2: continue
            fx = cx + gx*wdt*0.16 + gy*skew*0.25
            fy = cy - gy*hgt*0.25 - hgt*0.12
            poly(c2, [(fx-wdt*0.075, fy), (fx-wdt*0.075+skew*0.2, fy-hgt*0.16),
                      (fx+wdt*0.075+skew*0.2, fy-hgt*0.16), (fx+wdt*0.075, fy)])
blanket(W*0.555, H*0.985, W*0.085, H*0.060, (220,84,84), (244,224,150))
blanket(W*0.70, H*0.965, W*0.072, H*0.052, (74,128,200), (210,228,248))

random.seed(33)
FLOW = [(252,222,84),(252,252,252),(244,128,168),(180,140,236),(244,168,72)]
for _ in range(46):
    fx = random.uniform(W*0.40, W*0.99)
    fy = random.uniform(mtop(fx)+H*0.04, H-4*S)
    if fy <= mtop(fx)+H*0.02: continue
    stem_h = random.uniform(7,14)*S * ascale(fx)
    col = random.choice(FLOW)
    line((70,150,72), max(0.9, 1.3*S*ascale(fx)), (fx, fy), (fx, fy-stem_h))
    cy = fy - stem_h
    pr = random.uniform(2.6, 4.2)*S * (0.6+0.6*ascale(fx))
    shape = random.random()
    if shape < 0.5:
        for k in range(5):
            a = 2*math.pi*k/5 - math.pi/2
            ell(col, fx+math.cos(a)*pr, cy+math.sin(a)*pr, pr*0.6, pr*0.6)
        ell((250,196,70), fx, cy, pr*0.55, pr*0.55)
    else:
        ell(col, fx, cy, pr, pr)
        ell(dk(col,0.78), fx, cy, pr*0.42, pr*0.42)

random.seed(91)
for _ in range(40):
    gx = random.uniform(W*0.40, W*0.99)
    gy = random.uniform(H*0.86, H-3*S)
    gh = random.uniform(4,8)*S
    line(dk((92,170,74),0.92), max(0.8,1.0*S), (gx, gy), (gx+random.uniform(-2,2)*S, gy-gh))
    line((110,190,96), max(0.8,1.0*S), (gx+2*S, gy), (gx+2*S+random.uniform(-2,2)*S, gy-gh*0.8))

people = []
rows = [(H*0.80, 34, 7), (H*0.875, 44, 7), (H*0.95, 56, 6)]
for ry, ph, n in rows:
    for i in range(n):
        px = W*0.42 + (W*0.56) * (i + random.uniform(-0.35, 0.35)) / (n-1)
        if px < W*0.40: continue
        pose = random.choices(["up", "oneup", "out"], weights=[5, 3, 2])[0]
        people.append((px, ry + random.uniform(-4, 4)*S, ph*S * random.uniform(0.9, 1.1), pose))
people.sort(key=lambda p: p[1])
for px, py, ph, pose in people:
    ell((64,138,66), px, py+ph*0.02, ph*0.26, ph*0.06)
for px, py, ph, pose in people:
    person(px, py, ph, pose)

sys.stdout.write(" ".join(out))
PY
)
convert "$w/sky6.png" -draw "$scene" "$w/scene.png"

# text labels — rendered at 2x, downscale crisp; over the open left sky
P() { echo $(( $1 * S )); }
convert -background none -font "$fr" -pointsize $(P 54) -kerning $((1*S)) -fill '#2f3d5e' label:'Nordstjernen ' "$w/t1.png"
convert -background none -font "$fr" -pointsize $(P 54) -fill '#d4862a' label:"$ver" "$w/t2.png"
convert -background none -font "$fr" -pointsize $(P 25) -fill '#3f6f8f' label:'Nordstjernen Web Browser' "$w/ts.png"
convert -background none -font "$fr" -pointsize $(P 23) -kerning $((3*S)) -fill '#d4862a' label:"$codename" "$w/tc.png"
convert -background none -font "$fr" -pointsize $(P 20) -fill '#5a7088' \
    label:'Étoile du Nord — the legendary web browser' "$w/t3.png"

w1=$(identify -format '%w' "$w/t1.png"); h1=$(identify -format '%h' "$w/t1.png")
hs=$(identify -format '%h' "$w/ts.png"); hc=$(identify -format '%h' "$w/tc.png")
g1=$((14*S)); g2=$((14*S)); g3=$((12*S))
ty=$((46*S)); textleft=$((80*S))
sy=$((ty + h1 + g1)); cy=$((sy + hs + g2)); gy=$((cy + hc + g3))

convert "$w/scene.png" \
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
