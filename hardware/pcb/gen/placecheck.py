# -*- coding: utf-8 -*-
"""Placement sanity: courtyard overlaps, parts off the board, and anything that
would poke through the back into the display glass (THT pads, NPTH pegs)."""
import re, sys, math
PCB = sys.argv[1]
s = open(PCB).read()
GLASS = (100.0, 107.18, 162.8, 150.0)   # v3: the whole board sits on the display glass
BOARD = (98.5, 107.18, 164.3, 149.2)
def close(t, i):
    d = 0
    for j in range(i, len(t)):
        if t[j] == '(': d += 1
        elif t[j] == ')':
            d -= 1
            if d == 0: return j + 1
fps = []
for m in re.finditer(r'\n\t\(footprint "', s):
    st = m.start() + 1; blk = s[st:close(s, st)]
    ref = re.search(r'\(property "Reference" "([^"]+)"', blk).group(1)
    at = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', blk)
    fx, fy, rot = float(at.group(1)), float(at.group(2)), float(at.group(3) or 0)
    r = math.radians(rot)
    X = lambda lx, ly: fx + lx*math.cos(r) + ly*math.sin(r)
    Y = lambda lx, ly: fy - lx*math.sin(r) + ly*math.cos(r)
    xs, ys = [], []
    for g in re.finditer(r'\(fp_(?:line|rect)\b', blk):
        gg = blk[g.start():close(blk, g.start())]
        if 'CrtYd' not in gg: continue
        for a, b in re.findall(r'\((?:start|end) ([-\d.]+) ([-\d.]+)\)', gg):
            xs.append(X(float(a), float(b))); ys.append(Y(float(a), float(b)))
    holes = []
    for pm in re.finditer(r'\(pad "([^"]*)" (thru_hole|np_thru_hole)', blk):
        g = blk[pm.start():close(blk, pm.start())]
        a = re.search(r'\(at ([-\d.]+) ([-\d.]+)', g)
        sz = re.search(r'\(size ([-\d.]+) ([-\d.]+)\)', g)
        lx, ly = float(a.group(1)), float(a.group(2))
        holes.append((pm.group(1), pm.group(2), X(lx, ly), Y(lx, ly), max(float(sz.group(1)), float(sz.group(2)))/2))
    if xs: fps.append((ref, min(xs), max(xs), min(ys), max(ys), holes))
    else: fps.append((ref, fx, fx, fy, fy, holes))
bad = 0
for i in range(len(fps)):
    for j in range(i+1, len(fps)):
        a, b = fps[i], fps[j]
        ox = min(a[2], b[2]) - max(a[1], b[1]); oy = min(a[4], b[4]) - max(a[3], b[3])
        if ox > 0.01 and oy > 0.01 and not (a[0].startswith('MH') or b[0].startswith('MH')):
            print('  OVERLAP  %-5s %-5s  %.2f x %.2f mm' % (a[0], b[0], ox, oy)); bad += 1
for f in fps:
    ref, x0, x1, y0, y1, holes = f
    if x0 < BOARD[0]-0.01 or x1 > BOARD[2]+0.01 or y0 < BOARD[1]-0.01 or y1 > BOARD[3]+0.01:
        print('  PAST EDGE %-5s courtyard x[%.2f,%.2f] y[%.2f,%.2f]' % (ref, x0, x1, y0, y1))
    for num, typ, hx, hy, hr in holes:
        if ref.startswith('MH') or (ref == 'U1' and num == '41'): continue
        if GLASS[0]-hr < hx < GLASS[2]+hr and GLASS[1]-hr < hy < GLASS[3]+hr:
            print('  UNDER GLASS %-5s pad %-3s %-12s at (%.2f,%.2f) r=%.2f' % (ref, num, typ, hx, hy, hr)); bad += 1
print('placement issues:', bad)
