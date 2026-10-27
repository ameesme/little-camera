# -*- coding: utf-8 -*-
"""Re-layout pass (2026-09-29): shutter top-left, vertical camera FPC top-right,
USB-C to the bottom edge, J3 opening toward the bottom edge, buzzer cluster
re-packed.  Rips up ALL tracks and vias so the board can be re-fanned-out and
re-routed from a clean state.  Nets come from design.CONN."""
import re, sys, os, uuid, math
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from design import CONN
ROOT = os.path.dirname(HERE)
PCB = os.path.join(ROOT, 'little_camera.kicad_pcb')
s = open(PCB).read()
U = lambda: str(uuid.uuid4())

def close(t, i):
    d = 0
    for j in range(i, len(t)):
        if t[j] == '(': d += 1
        elif t[j] == ')':
            d -= 1
            if d == 0: return j + 1

def fp_span(ref):
    i = s.find('(property "Reference" "%s"' % ref)
    st = s.rfind('\n\t(footprint "', 0, i) + 1
    return st, close(s, st)

def fmt(v):
    v = round(v, 4)
    return ('%g' % v)

def rot_ats(blk, delta, fp_at_new):
    """Set the footprint-level (at) and add `delta` to every other (at x y a)."""
    m = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', blk)
    head, tail = blk[:m.start()], blk[m.end():]
    def f(mm):
        a = float(mm.group(3) or 0) + delta
        a = ((a + 180) % 360) - 180
        a = 0 if abs(a) < 1e-9 else a
        return '(at %s %s%s)' % (mm.group(1), mm.group(2), (' ' + fmt(a)) if a else '')
    tail = re.sub(r'\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', f, tail)
    x, y, r = fp_at_new
    return head + '\n\t\t(at %s %s%s)' % (fmt(x), fmt(y), (' ' + fmt(r)) if r else '') + tail

def move(ref, x, y, r):
    global s
    st, en = fp_span(ref); blk = s[st:en]
    m = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', blk)
    old = float(m.group(3) or 0)
    s = s[:st] + rot_ats(blk, r - old, (x, y, r)) + s[en:]

def place_from_mod(ref, libname, modfile, value, x, y, r):
    """Replace footprint `ref` with the one in lc.pretty/<modfile>, nets from CONN."""
    global s
    st, en = fp_span(ref); old = s[st:en]
    fpu = re.search(r'\n\t\t\(uuid "([^"]+)"\)', old).group(1)
    path = re.search(r'\n\t\t\(path "([^"]+)"\)', old)
    mod = open(os.path.join(ROOT, 'lc.pretty', modfile)).read().strip()
    mod = re.sub(r'\n\t\((version|generator|generator_version) [^\n]*\)', '', mod)
    mod = mod.replace('(footprint "%s"' % modfile[:-10], '(footprint "%s"' % libname, 1)
    mod = mod.replace('(property "Reference" "REF**"', '(property "Reference" "%s"' % ref, 1)
    mod = re.sub(r'\(property "Value" "[^"]*"', '(property "Value" "%s"' % value, mod, count=1)
    conn = {str(k): v for k, v in CONN[ref].items()}
    def padfix(mm):
        g = mm.group(0); num = mm.group(1)
        net = conn.get(num)
        extra = (' (net "%s")' % net) if net else ''
        return g[:-1] + extra + ' (uuid "%s"))' % U()
    # pads are single-line in our lc mods
    mod = re.sub(r'\(pad "([^"]*)" [^\n]*\)', padfix, mod)
    # indent one level and insert uuid/at after (layer "F.Cu")
    lines = mod.split('\n')
    out = ['\t' + lines[0]] + ['\t' + l for l in lines[1:]]
    blk = '\n'.join(out)
    ins = '\n\t\t(uuid "%s")\n\t\t(at 0 0)' % fpu
    if path: ins += '\n\t\t(path "%s")' % path.group(1)
    blk = blk.replace('\n\t\t(layer "F.Cu")', '\n\t\t(layer "F.Cu")' + ins, 1)
    blk = '\t' + rot_ats(blk.lstrip('\t'), r, (x, y, r))
    s = s[:st] + blk + s[en:]

# ------------------------------------------------------------ placements --
place_from_mod('J1', 'lc:USB_C_Kinghelm_KH-TYPE-C-W-SMT-16P',
               'USB_C_Kinghelm_KH-TYPE-C-W-SMT-16P.kicad_mod', 'USB-C', 152.7, 145.8, 0)
place_from_mod('J2', 'lc:Hirose_FH12-24S-0.5SVA_Vertical',
               'Hirose_FH12-24S-0.5SVA_Vertical.kicad_mod', 'OV2640 DVP', 151.0, 105.0, 180)
place_from_mod('SW1', 'lc:SW_Omron_D2F_PCB', 'SW_Omron_D2F_PCB.kicad_mod', 'OMRON D2F', 111.0, 102.3, 180)
# SW1 is through-hole
st, en = fp_span('SW1'); s = s[:st] + s[st:en].replace('(attr smd)', '(attr through_hole)') + s[en:]

MOVES = {
    'R26': (119.6, 102.6, 0), 'C24': (119.6, 104.2, 0),
    'R8': (122.9, 102.6, 0), 'R9': (125.1, 102.6, 0), 'C6': (127.3, 102.6, 0),
    'BZ1': (155.5, 136.2, 0), 'Q2': (150.2, 136.2, 90),
    'R27': (150.5, 139.2, 0), 'R28': (152.9, 139.8, 0),
    'D4': (161.0, 137.0, 90),
    'R1': (150.3, 141.0, 0), 'R2': (155.2, 141.0, 0), 'C1': (157.8, 141.2, 0),
    'J3': (131.9, 145.0, 0),
}
for ref, (x, y, r) in MOVES.items(): move(ref, x, y, r)

# ------------------------------------------------------------ rip-up -----
n = 0
for tag in ('segment', 'via', 'arc'):
    while True:
        m = re.search(r'\n\t\(' + tag + r'\b', s)
        if not m: break
        st = m.start() + 1; en = close(s, st)
        s = s[:m.start()] + s[en:]; n += 1
# drop stale zone fills; KiCad refills
k = 0
while True:
    m = re.search(r'\n\t\t\(filled_polygon\b', s)
    if not m: break
    st = m.start() + 1; en = close(s, st); s = s[:m.start()] + s[en:]; k += 1
open(PCB, 'w').write(s)
print('placed J1/J2/SW1, moved', len(MOVES), 'parts, ripped up', n, 'tracks/vias, dropped', k, 'fill polygons')
