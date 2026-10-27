# -*- coding: utf-8 -*-
"""Independent connectivity proof for the routed board.

For every net: build a graph of pads, track segments, vias and filled zone
polygons, union everything that physically touches on a shared copper layer,
and demand that all of the net's pads end up in ONE component.
Nothing here trusts KiCad's DRC - that is the point.
"""
import re, math, sys
from collections import defaultdict

F = sys.argv[1]
src = open(F).read()
CU = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"]

def blocks(s, tag, start=0, end=None):
    end = len(s) if end is None else end
    out = []; i = start
    while True:
        i = s.find(tag, i, end)
        if i < 0: break
        d = 0; j = i
        while s[j] != '(': j += 1
        i2 = j
        while True:
            c = s[j]
            if c == '"':
                j += 1
                while s[j] != '"': j += 1
            elif c == '(': d += 1
            elif c == ')':
                d -= 1
                if d == 0: break
            j += 1
        out.append(s[i:j+1].lstrip()); i = j + 1
    return out

def rot(x, y, a):
    r = math.radians(a); c, s_ = math.cos(r), math.sin(r)
    return (x*c + y*s_, -x*s_ + y*c)

NET = re.compile(r'\(net "([^"]*)"\)')

# ---------------------------------------------------------------- pads -----
pads = []   # dict(ref,num,net,x,y,w,h,ang,layers)
for fp in blocks(src, "(footprint "):
    ref = re.search(r'\(property "Reference" "([^"]+)"', fp)
    ref = ref.group(1) if ref else "?"
    head = fp[:fp.find("(pad ")] if "(pad " in fp else fp
    m = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', head)
    fx, fy, fa = float(m.group(1)), float(m.group(2)), float(m.group(3) or 0)
    flayer = re.search(r'\n\t\t\(layer "([^"]+)"\)', head).group(1)
    for pb in blocks(fp, "(pad "):
        n = NET.search(pb)
        netname = n.group(1) if n else ""        # no-net pads are still copper
        num = re.match(r'\(pad "([^"]*)"', pb).group(1)
        pm = re.search(r'\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', pb)
        px, py = float(pm.group(1)), float(pm.group(2))
        pang = float(pm.group(3) or fa)
        sm = re.search(r'\(size ([-\d.]+) ([-\d.]+)\)', pb)
        w, h = float(sm.group(1)), float(sm.group(2))
        lm = re.search(r'\(layers ([^)]*)\)', pb).group(1)
        if '*.Cu' in lm: lays = CU[:]
        else: lays = [l for l in CU if f'"{l}"' in lm]
        dx, dy = rot(px, py, fa)
        pads.append(dict(ref=ref, num=num, net=netname, x=fx+dx, y=fy+dy,
                         w=w, h=h, ang=pang, layers=lays))

def in_pad(p, x, y, slack=0.02):
    dx, dy = x - p["x"], y - p["y"]
    lx, ly = rot(dx, dy, -p["ang"])
    return abs(lx) <= p["w"]/2 + slack and abs(ly) <= p["h"]/2 + slack

# ------------------------------------------------------- tracks & vias -----
top = src  # board-level items are outside footprints; footprints have no segments
segs, vias = [], []
for b in blocks(top, "\n\t(segment\n"):
    n = NET.search(b)
    a = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)', b); e = re.search(r'\(end ([-\d.]+) ([-\d.]+)\)', b)
    w = float(re.search(r'\(width ([-\d.]+)\)', b).group(1))
    L = re.search(r'\(layer "([^"]+)"\)', b).group(1)
    segs.append(dict(net=n.group(1), x1=float(a.group(1)), y1=float(a.group(2)),
                     x2=float(e.group(1)), y2=float(e.group(2)), w=w, layer=L))
for b in blocks(top, "\n\t(via\n"):
    n = NET.search(b)
    a = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', b)
    sz = float(re.search(r'\(size ([-\d.]+)\)', b).group(1))
    vias.append(dict(net=n.group(1), x=float(a.group(1)), y=float(a.group(2)), r=sz/2))

# ---------------------------------------------------------------- zones ----
zpolys = []  # (net, layer, [pts])
for zb in blocks(top, "\n\t(zone\n"):
    n = NET.search(zb)
    if not n or not n.group(1): continue
    for fp_ in blocks(zb, "(filled_polygon"):
        L = re.search(r'\(layer "([^"]+)"\)', fp_).group(1)
        pts = [(float(a), float(b)) for a, b in re.findall(r'\(xy ([-\d.]+) ([-\d.]+)\)', fp_)]
        zpolys.append((n.group(1), L, pts))

def in_poly(x, y, pts):
    c = False; n = len(pts); j = n - 1
    for i in range(n):
        xi, yi = pts[i]; xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi + 1e-12) + xi:
            c = not c
        j = i
    return c

def seg_dist(px, py, s):
    x1, y1, x2, y2 = s["x1"], s["y1"], s["x2"], s["y2"]
    dx, dy = x2-x1, y2-y1; L2 = dx*dx+dy*dy
    t = 0 if L2 == 0 else max(0, min(1, ((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-(x1+t*dx), py-(y1+t*dy))

# ------------------------------------------------------------ union-find ---
par = {}
def f(a):
    while par[a] != a:
        par[a] = par[par[a]]; a = par[a]
    return a
def u(a, b): par[f(a)] = f(b)

bynet = defaultdict(lambda: dict(p=[], s=[], v=[], z=[]))
for i, p in enumerate(pads):  bynet[p["net"]]["p"].append(i); par[("p",i)] = ("p",i)
for i, s in enumerate(segs):  bynet[s["net"]]["s"].append(i); par[("s",i)] = ("s",i)
for i, v in enumerate(vias):  bynet[v["net"]]["v"].append(i); par[("v",i)] = ("v",i)
for i, z in enumerate(zpolys): bynet[z[0]]["z"].append(i); par[("z",i)] = ("z",i)

for net, g in bynet.items():
    P, S, V, Z = g["p"], g["s"], g["v"], g["z"]
    # segment <-> segment : endpoint on the other segment, same layer
    for a in S:
        for b in S:
            if a >= b or segs[a]["layer"] != segs[b]["layer"]: continue
            sa, sb = segs[a], segs[b]; tol = (sa["w"]+sb["w"])/2 * 0.5 + 0.01
            if (seg_dist(sa["x1"],sa["y1"],sb) < tol or seg_dist(sa["x2"],sa["y2"],sb) < tol or
                seg_dist(sb["x1"],sb["y1"],sa) < tol or seg_dist(sb["x2"],sb["y2"],sa) < tol):
                u(("s",a),("s",b))
    # segment <-> pad
    for a in S:
        s = segs[a]
        for pi in P:
            p = pads[pi]
            if s["layer"] not in p["layers"]: continue
            if in_pad(p, s["x1"], s["y1"]) or in_pad(p, s["x2"], s["y2"]): u(("s",a),("p",pi))
    # via <-> segment / pad
    for vi in V:
        v = vias[vi]
        for a in S:
            if seg_dist(v["x"], v["y"], segs[a]) < v["r"] + segs[a]["w"]/2 - 0.01: u(("v",vi),("s",a))
        for pi in P:
            if in_pad(pads[pi], v["x"], v["y"], slack=v["r"]): u(("v",vi),("p",pi))
    # zone <-> via / THT pad / pad on that layer / segment endpoint
    for zi in Z:
        _, L, pts = zpolys[zi]
        xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
        bb=(min(xs),min(ys),max(xs),max(ys))
        for vi in V:
            v=vias[vi]
            if not (bb[0]-1<v["x"]<bb[2]+1 and bb[1]-1<v["y"]<bb[3]+1): continue
            # solid-connected vias sit inside the fill; thermal ones meet it at spoke ends
            if in_poly(v["x"], v["y"], pts) or any(
                    math.hypot(qx-v["x"],qy-v["y"]) <= v["r"]+0.05 for qx,qy in pts):
                u(("z",zi),("v",vi))
        for pi in P:
            p = pads[pi]
            if L not in p["layers"]: continue
            if not (bb[0]-3<p["x"]<bb[2]+3 and bb[1]-3<p["y"]<bb[3]+3): continue
            # thermal relief: spokes end ON the pad edge, so a fill vertex lands
            # inside the pad outline even though the pad centre is not filled
            if in_poly(p["x"], p["y"], pts) or any(in_pad(p, qx, qy, slack=0.06) for qx,qy in pts):
                u(("z",zi),("p",pi))
        for a in S:
            s = segs[a]
            if s["layer"] == L and (in_poly(s["x1"],s["y1"],pts) or in_poly(s["x2"],s["y2"],pts)):
                u(("z",zi),("s",a))
    # pad <-> pad (same footprint, overlapping, e.g. stacked GND pads)
    for i in P:
        for j in P:
            if i < j and set(pads[i]["layers"]) & set(pads[j]["layers"]):
                if in_pad(pads[i], pads[j]["x"], pads[j]["y"]) or in_pad(pads[j], pads[i]["x"], pads[i]["y"]):
                    u(("p",i),("p",j))

# ---------------------------------------------------------------- report ---
bad = []
for net, g in sorted(bynet.items()):
    P = g["p"]
    if net == "" or len(P) < 2: continue
    comps = defaultdict(list)
    for pi in P: comps[f(("p",pi))].append(f'{pads[pi]["ref"]}.{pads[pi]["num"]}')
    if len(comps) > 1:
        groups = sorted(comps.values(), key=len, reverse=True)
        bad.append((net, groups))

print(f"pads with nets: {len(pads)}   segments: {len(segs)}   vias: {len(vias)}   zone fills: {len(zpolys)}")
nets_multi = sum(1 for k, g in bynet.items() if k and len(g['p']) >= 2)
print(f"nets with 2+ pads: {nets_multi}   fully connected: {nets_multi - len(bad)}   BROKEN: {len(bad)}")
for net, groups in bad:
    print(f"\n  {net}: {len(groups)} islands")
    for gr in groups:
        print("     ", " ".join(sorted(gr)))
