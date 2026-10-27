import re, math
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


npth=[]
for fp in blocks(src, "(footprint "):
    if "np_thru_hole" not in fp: continue
    head = fp[:fp.find("(pad ")]
    m = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)', head)
    d = re.search(r'np_thru_hole[\s\S]*?\(drill ([\d.]+)\)', fp)
    npth.append((float(m.group(1)), float(m.group(2)), float(d.group(1))/2))
def seg_dist(px, py, s):
    x1, y1, x2, y2 = s["x1"], s["y1"], s["x2"], s["y2"]
    dx, dy = x2-x1, y2-y1; L2 = dx*dx+dy*dy
    t = 0 if L2 == 0 else max(0, min(1, ((px-x1)*dx+(py-y1)*dy)/L2))
    return math.hypot(px-(x1+t*dx), py-(y1+t*dy))
