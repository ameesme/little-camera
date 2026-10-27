# LS027B7DH01 v3q: two glass layers (TFT rear, full size; CF front, set back 2.5 mm at the FPC edge = ledge),
# FPC bonded on the viewer side of the TFT ledge, real tail length (~13.5 mm from the glass edge, photo estimate),
# 9.4 mm wide near the glass, 5.5 mm further on, 4.6 mm stiffener (0.3 mm total) inserted 3.15 mm into J3,
# formed into the S-fold that fits the v3 bottom margin. Model coords: origin = glass centre (lens 31.4, 21.41),
# X = lens x - 31.4, Y = lens y - 21.41, Z = 0 at the board top (board 0.8, tape 0.1, display back at -0.9).
import math, sys, numpy as np, cadquery as cq
from cadquery import Color
from PIL import Image
from shapely.geometry import box as sbox
from shapely.ops import unary_union
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from fpc_geom import path_S
OUT = '/home/claude/v3/models/'
TFTC = Color(0.55, 0.6, 0.62); CFC = Color(0.62, 0.67, 0.69); POLC = Color(0.16, 0.17, 0.17); BEZEL = Color(0.30, 0.31, 0.31)
LW = Color(0.80, 0.81, 0.78); LB = Color(0.11, 0.12, 0.13); FLEX = Color(0.85, 0.5, 0.1); STIFF = Color(0.07, 0.07, 0.07)
def B(x0, x1, y0, y1, z0, z1): return cq.Workplane('XY').box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))
GX, GY = 31.4, 21.41; H2 = 21.41; W2 = 31.4
ZB = -0.9; ZT = -1.6; ZC = -2.33; ZP = -2.53; LEDGE = 1.7      # v3q: CF glass ends 1.7 mm before the TFT edge (photo)
AX, AY = 29.4, 17.64; P = 0.147; AYC = 1.71   # v3q: active area sits 2.06 mm from the top edge, 5.48 mm from the FPC edge (photo, normalised to the 35.28 spec height)
parts = [(B(-W2, W2, -H2, H2, ZT, ZB), TFTC),                                  # TFT glass (rear, full 62.8 x 42.82)
         (B(-W2, W2, -H2 + LEDGE, H2, ZC, ZT), CFC),                            # CF glass (front), starts 2.5 mm higher
         (B(-W2 + 0.3, W2 - 0.3, -H2 + LEDGE + 0.3, H2 - 0.3, ZP, ZC).cut(B(-AX, AX, AYC - AY, AYC + AY, ZP - 0.1, ZC + 0.01)), POLC),
         (B(-AX, AX, AYC - AY, AYC + AY, ZP, ZC), LW)]
img = np.array(Image.open('/home/claude/models/splash_400x240.png')); rects = []
for v in range(240):
    row = ~img[v]; u = 0
    while u < 400:
        if row[u]:
            u0 = u
            while u < 400 and row[u]: u += 1
            rects.append(sbox(AX - u * P, AYC + AY - (v + 1) * P, AX - u0 * P, AYC + AY - v * P))
        else: u += 1
g = unary_union(rects).simplify(1e-6); polys = list(g.geoms) if g.geom_type == 'MultiPolygon' else [g]
def wire(cs): return cq.Wire.makePolygon([cq.Vector(x, y, 0) for x, y in list(cs)[:-1]], close=True)
sol = [cq.Solid.extrudeLinear(cq.Face.makeFromWires(wire(p.exterior.coords), [wire(i.coords) for i in p.interiors]), cq.Vector(0, 0, 0.006)).translate((0, 0, ZP - 0.006)) for p in polys]
parts.append((cq.Workplane().add(cq.Compound.makeCompound(sol)), LB))
# ---------------- FPC (lens y/z section, see /home/claude/fpc/geom.py) ----------------
L = 13.5; p = path_S(L); T = 0.10; zf = -1.65
Yl = lambda y: y - GY                       # lens y -> model Y
def strip(segs, w):
    """segs: list of ('L',(y0,z0),(y1,z1)) or ('A',(cy,cz),r,a0,a1) in lens y/z; returns a solid of thickness T, width w"""
    out = None
    for s in segs:
        if s[0] == 'L':
            (y0, z0), (y1, z1) = s[1], s[2]
            if abs(y1 - y0) < 1e-9:
                sh = B(-w / 2, w / 2, Yl(y0) - T / 2, Yl(y0) + T / 2, min(z0, z1), max(z0, z1))
            else:
                sh = B(-w / 2, w / 2, Yl(min(y0, y1)), Yl(max(y0, y1)), z0 - T / 2, z0 + T / 2)
        else:
            (cy, cz), r, a0, a1 = s[1], s[2], s[3], s[4]
            ring = cq.Workplane('YZ').center(Yl(cy), cz).circle(r + T / 2).circle(r - T / 2).extrude(w).translate((-w / 2, 0, 0))
            m = (a0 + a1) / 2; R2 = r + 1
            pts = [(Yl(cy), cz)] + [(Yl(cy) + R2 * 2 * math.cos(math.radians(a)), cz + R2 * 2 * math.sin(math.radians(a))) for a in np.linspace(a0, a1, 9)]
            wedge = cq.Workplane('YZ').polyline(pts).close().extrude(w + 2).translate((-w / 2 - 1, 0, 0))
            sh = ring.intersect(wedge)
        out = sh if out is None else out.union(sh)
    return out
Rg, Ra, Rq, Rf, yd = p['Rg'], p['Ra'], p['Rq'], p['Rf'], p['yd']
wide = [('L', (1.5, zf), (0.0, zf)),                                   # bonded on the ledge (viewer side of the TFT glass)
        ('A', (0.0, zf + Rg), Rg, 180, 270),                            # quarter up at the glass edge
        ('L', (-Rg, zf + Rg), (-Rg, p['za']))]                          # leg along the wall
narrow = [('A', (-Rg + Ra, p['za']), Ra, 0, 180),                       # arch
          ('L', (yd, p['za']), (yd, p['zq'])),
          ('A', (yd - Rq, p['zq']), Rq, 270, 360),
          ('L', (yd - Rq, p['zl']), (p['y0s'], p['zl'])) if yd - Rq > p['y0s'] + 1e-6 else None,
          ('A', (p['y0s'], p['zs'] + Rf), Rf, 90, 270)]                 # 180 down into the stiffener
narrow = [s for s in narrow if s]
parts.append((strip(wide, 9.4), FLEX))
parts.append((strip(narrow, 5.5), FLEX))
parts.append((B(-2.75, 2.75, Yl(p['y0s']), Yl(p['ye']), p['zs'] - 0.15, p['zs'] + 0.15), STIFF))   # stiffener 4.6 x 0.3, 3.15 inside J3
a = cq.Assembly(name='MECH_LS027B7DH01_v3q')
for i, (sh, c) in enumerate(parts): a.add(sh, name='MECH_LS027B7DH01_v3q_%d' % i, color=c)
a.save(OUT + 'MECH_LS027B7DH01_v3q.step'); print('wrote, tail za %.3f top %.3f' % (p['za'], p['top']))
