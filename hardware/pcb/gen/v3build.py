# -*- coding: utf-8 -*-
"""v3 step 1: strip the v2 board down to a clean, unrouted, unplaced v3 frame.
 - all tracks/vias/arcs removed, every gr_* removed (silk art, notes, old outline)
 - MH1..MH4 removed (v3 has no mounting holes)
 - 0.8 mm 4-layer stackup, aux origin at the display glass lower-left
 - new Edge.Cuts: display outline (62.8 x 42.82) with the cell + camera cutout
 - zones: GND on F.Cu / In1 / B.Cu (In2 becomes a signal layer, its +3V3 zone is dropped)
 Coordinates: lens view (x right, y up), origin = display glass lower-left; KiCad = (100+x, 150-y)."""
import re, sys, os, uuid
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'relayout.py')).read().split('# ------------------------------------------------------------ placements --')[0])
s = open(os.path.join(ROOT, '..', 'little_camera_v2', 'little_camera.kicad_pcb')).read()   # always start from the v2 board
K = lambda x, y: (round(100 + x, 4), round(150 - y, 4))
def drop(pattern):
    global s
    n = 0
    while True:
        m = re.search(pattern, s)
        if not m: return n
        st = m.start() + 1; en = close(s, st); s = s[:m.start()] + s[en:]; n += 1
n1 = sum(drop(r'\n\t\(' + t + r'\b') for t in ('segment', 'via', 'arc'))
n2 = drop(r'\n\t\(gr_\w+')
n3 = drop(r'\n\t\(zone\b')
for ref in ('MH1', 'MH2', 'MH3', 'MH4'):
    st, en = fp_span(ref); s = s[:st - 1] + s[en:]
# stackup / thickness / origin
s = re.sub(r'\(general\s*\(thickness [\d.]+\)', '(general\n\t\t(thickness 0.8)', s, count=1)
s = s.replace('(thickness 1.24)', '(thickness 0.45)', 1)
s = re.sub(r'\(aux_axis_origin [-\d.]+ [-\d.]+\)', '(aux_axis_origin 98.5 149.2)', s)   # v3o: board lower-left
# ---------------- outline (lens coords)
W, H, YB = 62.8, 42.82, 0.8
CUT = [(19.6, 6.8), (48.9, 6.8), (50.1, 8.0), (50.1, 36.9), (19.6, 36.9)]   # v3e: camera sits ON the board (no camera cutout)
XL = -1.5   # v3o: 1.5 mm past the glass on both sides (display centred)
XR = W + 1.5
# U-notches for M1.2 screws: boss centre 0.8 mm in from the edge, slot half-width 1.3 (boss OD 2.4 + 0.1 play)
NOTCH_L = (4.0, 38.4); NOTCH_R = (4.0, 38.4); NW = 0.8   # v3o: r0.8 locating indents (keys in the case), symmetric
def poly(pts):
    out = ''
    for i in range(len(pts)):
        a, b = K(*pts[i]), K(*pts[(i + 1) % len(pts)])
        out += '\n\t(gr_line (start %g %g) (end %g %g) (stroke (width 0.1) (type default)) (layer "Edge.Cuts") (uuid "%s"))' % (a[0], a[1], b[0], b[1], U())
    return out
def seg(a, b):
    a, b = K(*a), K(*b)
    return '\n\t(gr_line (start %g %g) (end %g %g) (stroke (width 0.1) (type default)) (layer "Edge.Cuts") (uuid "%s"))' % (a[0], a[1], b[0], b[1], U())
def arc(a, m, b):
    a, m, b = K(*a), K(*m), K(*b)
    return '\n\t(gr_arc (start %g %g) (mid %g %g) (end %g %g) (stroke (width 0.1) (type default)) (layer "Edge.Cuts") (uuid "%s"))' % (a[0], a[1], m[0], m[1], b[0], b[1], U())
# v3p: mid-mount USB-C (SHOU HAN CB0.8) - shell front flush with the case outside (lens y -2.1).
# The board gets a small tab down to y -1.3 under the connector's front slots, with the 9.24-wide
# body cutout open to the tab edge (back of cutout 2.25 behind the footprint origin).
J1X, J1Y = 56.1, 2.26; TAB = -1.3; TXH = 6.5; CXH = 4.62; CT = J1Y + 2.25
def bottom():
    pts = [(XL, YB), (J1X - TXH, YB), (J1X - TXH, TAB), (J1X - CXH, TAB), (J1X - CXH, CT),
           (J1X + CXH, CT), (J1X + CXH, TAB), (J1X + TXH, TAB), (J1X + TXH, YB), (XR, YB)]
    return ''.join(seg(pts[i], pts[i + 1]) for i in range(len(pts) - 1))
def outline():
    g = bottom(); y = YB
    for yc in NOTCH_R:                               # right edge, going up
        g += seg((XR, y), (XR, yc - NW)) + arc((XR, yc - NW), (XR - NW, yc), (XR, yc + NW)); y = yc + NW
    g += seg((XR, y), (XR, H)) + seg((XR, H), (XL, H)); y = H
    for yc in sorted(NOTCH_L, reverse=True):         # left edge, going down
        g += seg((XL, y), (XL, yc + NW)) + arc((XL, yc + NW), (XL + NW, yc), (XL, yc - NW)); y = yc - NW
    g += seg((XL, y), (XL, YB))
    return g
geo = outline() + poly(CUT)
# v3p: silk art (gen/silkart_v3.py): amaranth signature on B.SilkS (display side), wordmark on F.SilkS between J5 and J2
_sa = os.path.join(HERE, 'silkart_v3.txt')
if os.path.exists(_sa): geo += '\n' + open(_sa).read().rstrip('\n')
# ---------------- zones
def zone(net, layer):
    x0, y0 = K(XL - 0.5, H + 0.5); x1, y1 = K(XR + 0.5, -1.5)
    return ('\n\t(zone\n\t\t(net "%s")\n\t\t(layer "%s")\n\t\t(uuid "%s")\n\t\t(hatch edge 0.5)\n\t\t(connect_pads\n\t\t\t(clearance 0.2)\n\t\t)\n'
            '\t\t(min_thickness 0.2)\n\t\t(fill yes\n\t\t\t(thermal_gap 0.3)\n\t\t\t(thermal_bridge_width 0.3)\n\t\t\t(island_removal_mode 0)\n\t\t)\n'
            '\t\t(polygon\n\t\t\t(pts\n\t\t\t\t(xy %g %g) (xy %g %g) (xy %g %g) (xy %g %g)\n\t\t\t)\n\t\t)\n\t)') % (net, layer, U(), x0, y0, x1, y0, x1, y1, x0, y1)
zs = zone('GND', 'F.Cu') + zone('GND', 'In1.Cu') + zone('GND', 'B.Cu')
i = s.rfind('\n\t(embedded_fonts')
s = s[:i] + geo + zs + s[i:]
open(PCB, 'w').write(s)
open(os.path.join(HERE, 'v3_frame.kicad_pcb'), 'w').write(s)
print('v3build: removed %d tracks/vias, %d graphics, %d zones, 4 MH; outline + 3 GND zones added' % (n1, n2, n3))
