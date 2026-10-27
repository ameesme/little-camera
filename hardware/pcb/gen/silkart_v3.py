# v3 silk art: B.SilkS (display side) = amaranth signature over the whole board, as in v2;
# F.SilkS = small "little camera" wordmark + "lttl.cam v1.1" between J5 (BAT) and J2 (camera socket).
import re, sys, math
exec(open('/mnt/user-data/uploads/xiao_shutter/little_camera_v2/gen/silkart_gen_v2.py').read().split('X0, W = 102.1, 17.0')[0])
from shapely.geometry import LineString, Point, box
from shapely.ops import polygonize, unary_union
PCB = sys.argv[1]
s = open(PCB).read()
segs = []
for m in re.finditer(r'\(gr_line \(start ([-\d.]+) ([-\d.]+)\) \(end ([-\d.]+) ([-\d.]+)\)[^\n]*Edge\.Cuts', s):
    a = tuple(map(float, m.group(1, 2))); b = tuple(map(float, m.group(3, 4))); segs.append(LineString([a, b]))
for m in re.finditer(r'\(gr_arc \(start ([-\d.]+) ([-\d.]+)\) \(mid ([-\d.]+) ([-\d.]+)\) \(end ([-\d.]+) ([-\d.]+)\)[^\n]*Edge\.Cuts', s):
    p = [tuple(map(float, m.group(i, i + 1))) for i in (1, 3, 5)]
    (x1, y1), (x2, y2), (x3, y3) = p
    d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    ux = ((x1**2 + y1**2) * (y2 - y3) + (x2**2 + y2**2) * (y3 - y1) + (x3**2 + y3**2) * (y1 - y2)) / d
    uy = ((x1**2 + y1**2) * (x3 - x2) + (x2**2 + y2**2) * (x1 - x3) + (x3**2 + y3**2) * (x2 - x1)) / d
    r = math.hypot(x1 - ux, y1 - uy)
    a1, a2, a3 = [math.atan2(y - uy, x - ux) for x, y in p]
    def unwrap(a, ref):
        while a - ref > math.pi: a -= 2 * math.pi
        while a - ref < -math.pi: a += 2 * math.pi
        return a
    a2 = unwrap(a2, a1); a3 = unwrap(a3, a2)
    pts = [(ux + r * math.cos(a1 + (a3 - a1) * t / 24), uy + r * math.sin(a1 + (a3 - a1) * t / 24)) for t in range(25)]
    pts[0] = p[0]; pts[-1] = p[2]
    segs.append(LineString(pts))
polys = list(polygonize(unary_union(segs)))
polys.sort(key=lambda p: -p.area)
board = polys[0]
for h in polys[1:]:
    if board.contains(h.representative_point()): board = board.difference(h)
print('board area', round(board.area, 1), 'polys', len(polys), 'bounds', [round(v, 2) for v in board.bounds])
BX0, BY0, BX1, BY1 = board.bounds
clip = board.buffer(-0.3, join_style=2)
# ---- B.SilkS: amaranth signature (cover), mirrored so it reads right from the display side
sig, _ = svg_geom('/mnt/user-data/uploads/xiao_shutter/pattern.svg')
W_, H_ = BX1 - BX0, BY1 - BY0
minx, miny, maxx, maxy = sig.bounds
s_ = max(W_ / (maxx - minx), H_ / (maxy - miny))
sig = affinity.scale(sig, s_, s_, origin=(minx, miny))
sig = affinity.translate(sig, BX0 - minx - ((maxx - minx) * s_ - W_) / 2, BY0 - miny - ((maxy - miny) * s_ - H_) / 2)
sig = affinity.scale(sig, -1, 1, origin=((BX0 + BX1) / 2, 0))
sig = sig.intersection(clip).buffer(-0.08, join_style=2).buffer(0.08, join_style=2)
if hasattr(sig, 'geoms'): sig = unary_union([g for g in sig.geoms if g.area > 0.05])
# ---- F.SilkS: wordmark between J5 and J2 (lens x 35.2..44.2, y 37.6..42.3 -> KiCad)
K = lambda x, y: (100 + x, 150 - y)
LX0, LX1, LY0, LY1 = 35.4, 44.0, 37.7, 42.2
kx0, ky0 = K(LX0, LY1); kx1, ky1 = K(LX1, LY0)
logo, _ = svg_geom('/mnt/user-data/uploads/xiao_shutter/logo.svg')
ver = text_geom('lttl.cam v1.1')
logo = fit(logo, 0, 0, w=kx1 - kx0)
ver = fit(ver, 0, logo.bounds[3] + 0.5, h=0.9)
blk = unary_union([logo, ver])
if blk.bounds[3] - blk.bounds[1] > ky1 - ky0:
    sc = (ky1 - ky0) / (blk.bounds[3] - blk.bounds[1]); logo = affinity.scale(logo, sc, sc, origin=(0, 0)); ver = affinity.scale(ver, sc, sc, origin=(0, 0)); blk = unary_union([logo, ver])
dx = (kx0 + kx1) / 2 - (blk.bounds[0] + blk.bounds[2]) / 2; dy = (ky0 + ky1) / 2 - (blk.bounds[1] + blk.bounds[3]) / 2
logo = affinity.translate(logo, dx, dy); ver = affinity.translate(ver, dx, dy)
for n, g in (('logo', logo), ('version', ver), ('signature(B)', sig)):
    print(n, [round(v, 2) for v in g.bounds], 'area', round(g.area, 1))
TF = tris(logo) + tris(ver); TB = tris(sig)
print('triangles F', len(TF), 'B', len(TB))
open('silkart_v3.txt', 'w').write(gr(TF, 'F.SilkS') + '\n' + gr(TB, 'B.SilkS') + '\n')
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as P
fig, axs = plt.subplots(1, 2, figsize=(16, 6))
for ax, T, title, mirror in ((axs[0], TF, 'component side (F.SilkS)', False), (axs[1], TB, 'display side (B.SilkS), seen from the back', True)):
    ax.set_facecolor('#111')
    for t in T:
        tt = [((BX0 + BX1) - x, y) for x, y in t] if mirror else t
        ax.add_patch(P(tt, closed=True, fc='white', ec='white', lw=0.2))
    for g in [board] :
        ext = list(g.exterior.coords); 
        if mirror: ext = [((BX0 + BX1) - x, y) for x, y in ext]
        ax.add_patch(P(ext, closed=True, fc='none', ec='#e0c040'))
        for r in g.interiors:
            c = list(r.coords); c = [((BX0 + BX1) - x, y) for x, y in c] if mirror else c
            ax.add_patch(P(c, closed=True, fc='none', ec='#e0c040'))
    ax.set_xlim(BX0 - 1, BX1 + 1); ax.set_ylim(BY1 + 1, BY0 - 1); ax.set_aspect('equal'); ax.set_title(title)
plt.tight_layout(); plt.savefig('silkart_v3_preview.png', dpi=110)
