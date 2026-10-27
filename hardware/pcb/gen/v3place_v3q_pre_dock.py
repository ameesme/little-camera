# -*- coding: utf-8 -*-
"""v3 step 2: place everything on the small board (lens-view coords, KiCad = (100+x, 150-y)).
Big parts at fixed spots, small parts shelf-packed into zones by function."""
import re, sys, os, math, json
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'relayout.py')).read().split('# ------------------------------------------------------------ placements --')[0])
s = open(os.path.join(HERE, 'v3_frame.kicad_pcb')).read()
K = lambda x, y: (round(100 + x, 4), round(150 - y, 4))
# footprint swaps
place_from_mod('J1', 'lc:USB_C_ShouHan_TYPE-C-16P-CB0.8-073', 'USB_C_ShouHan_TYPE-C-16P-CB0.8-073.kicad_mod', 'USB-C', 0, 0, 0)   # v3p: mid-mount
place_from_mod('SW1', 'lc:SW_Omron_D2MQ-1-TR', 'SW_Omron_D2MQ-1-TR.kicad_mod', 'D2MQ-1-TR', 0, 0, 0)
place_from_mod('J4', 'lc:TestPads_1x06_P1.27', 'TestPads_1x06_P1.27.kicad_mod', 'SPARE IO', 0, 0, 0)
place_from_mod('J5', 'lc:BAT_Pads_Small', 'BAT_Pads_Small.kicad_mod', 'BAT', 0, 0, 0)
place_from_mod('SW2', 'lc:SW_Pads_Short', 'SW_Pads_Short.kicad_mod', 'BOOT', 0, 0, 0)
place_from_mod('SW3', 'lc:SW_Pads_Short', 'SW_Pads_Short.kicad_mod', 'RESET', 0, 0, 0)
# v3i: R30 dropped from the design; v3q: R18 and R29 too (panel always on)
for _r in ('R30', 'R18', 'R29'):
    _st, _en = fp_span(_r); s = s[:_st - 1] + s[_en:]
# v3q: new parts - clone R1's block as a placeholder (fresh uuids, no schematic path), then swap in the footprint
def add_part(ref):
    global s
    st, en = fp_span('R1'); blk = s[st:en]
    blk = blk.replace('(property "Reference" "R1"', '(property "Reference" "%s"' % ref, 1)
    blk = re.sub(r'\(uuid "[^"]+"\)', lambda m: '(uuid "%s")' % U(), blk)
    blk = re.sub(r'\n\t\t\(path "[^"]+"\)', '', blk)
    s = s[:st] + blk + '\n' + s[st:]
for _r in ('D5', 'D6'):
    add_part(_r)
    place_from_mod(_r, 'lc:D_DFN1006-2', 'D_DFN1006-2.kicad_mod', 'ESD9L5.0ST5G-N', 0, 0, 0)
# v3g: camera LDOs to X2SON 1x1, buzzer driver to SOT-323 / SOD-323
for _r, _v in (('U4', 'TLV75533PDQNR'), ('U5', 'TLV75528PDQNR'), ('U6', 'TLV75512PDQNR')):
    place_from_mod(_r, 'lc:LDO_X2SON-4_1x1_P0.65', 'LDO_X2SON-4_1x1_P0.65.kicad_mod', _v, 0, 0, 0)
place_from_mod('Q2', 'Package_TO_SOT_SMD:SOT-323_SC-70', 'SOT-323_SC-70.kicad_mod', 'DMG1012UW-7', 0, 0, 0)   # v3q
place_from_mod('D4', 'Diode_SMD:D_SOD-323', 'D_SOD-323.kicad_mod', '1N4148WS', 0, 0, 0)
place_from_mod('U8', 'Package_TO_SOT_SMD:SOT-353_SC-70-5', 'SOT-353_SC-70-5.kicad_mod', '74LVC1G14GW', 0, 0, 0)
FIXED = {'J4': (24.4, 41.4, 0), 'J5': (32.2, 41.0, 0), 'U1': (8.3, 13.85, 180), 'SW1': (4.5, 34.05, 0), 'J3': (31.4, 3.95, 0), 'J2': (53.8, 39.82, 180),
         'J1': (56.1, 2.26, 0), 'MECH1': (31.4, 21.41, 0), 'MECH2': (34.85, 21.85, 180), 'MECH3': (54.65, 21.41, 0)}
FIXED.update(json.load(open(os.path.join(HERE, 'v3_fixed_extra.json'))) if os.path.exists(os.path.join(HERE, 'v3_fixed_extra.json')) else {})
ZONES = json.load(open(os.path.join(HERE, 'v3_zones.json')))
def crt(ref):
    st, en = fp_span(ref); blk = s[st:en]; xs = []; ys = []
    for g in re.finditer(r'\(fp_(?:line|rect)\b', blk):
        gg = blk[g.start():close(blk, g.start())]
        if 'CrtYd' not in gg: continue
        for a, b in re.findall(r'\((?:start|end) ([-\d.]+) ([-\d.]+)\)', gg): xs.append(float(a)); ys.append(float(b))
    return min(xs), max(xs), min(ys), max(ys)
def rbox(c, rot):
    r = math.radians(rot); pts = []
    for lx in (c[0], c[1]):
        for ly in (c[2], c[3]):
            pts.append((lx * math.cos(r) + ly * math.sin(r), -lx * math.sin(r) + ly * math.cos(r)))
    return min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts), max(p[1] for p in pts)
for ref, (x, y, r) in FIXED.items():
    kx, ky = K(x, y); move(ref, kx, ky, r)

# U1's antenna keep-out rule area is stored in absolute coordinates inside the footprint and does not
# follow move(): put it under the antenna end of the module again (antenna at lens bottom-left in v3).
st, en = fp_span('U1'); blk = s[st:en]
kx0, ky0 = K(-1.5, 0.8); kx1, ky1 = K(18.1, 7.25)   # v3g: module 17.3 + 0.8 margin
blk2 = re.sub(r'\(xy 157\.25 111\) \(xy 157\.25 132\.5\) \(xy 163\.8 132\.5\) \(xy 163\.8 111\)',
              '(xy %g %g) (xy %g %g) (xy %g %g) (xy %g %g)' % (kx0, ky1, kx1, ky1, kx1, ky0, kx0, ky0), blk)
assert blk2 != blk, 'antenna keepout not found'
s = s[:st] + blk2 + s[en:]

# v3g: re-net U1 from design.CONN (GPIO map changed; the v2 frame still carries the old pad nets)
def renet(ref):
    global s
    st, en = fp_span(ref); blk = s[st:en]; conn = {str(k): v for k, v in CONN[ref].items()}
    out = []; i = 0; n = 0
    for m in re.finditer(r'\(pad "([^"]*)"', blk):
        ps = m.start(); pe = close(blk, ps); pad = blk[ps:pe]; num = m.group(1)
        if num in conn:
            pad2 = re.sub(r'\s*\(net "[^"]*"\)', '', pad)
            if conn[num]:
                pad2 = re.sub(r'(\(layers [^)]*\))', r'\1 (net "%s")' % conn[num], pad2, count=1)
            if pad2 != pad: n += 1
            out.append(blk[i:ps] + pad2); i = pe
    out.append(blk[i:]); s = s[:st] + ''.join(out) + s[en:]
    return n
print('U1 pads re-netted:', renet('U1'), ' J2:', renet('J2'))
# v3q: every footprint re-netted and re-valued from design.py (the v2 frame still carries v2/v3p nets and values)
from design import PARTS
_nr = 0
for _ref in sorted(CONN):
    if '(property "Reference" "%s"' % _ref not in s: continue
    _nr += renet(_ref)
    st, en = fp_span(_ref); blk = s[st:en]
    blk2 = re.sub(r'\(property "Value" "[^"]*"', '(property "Value" "%s"' % PARTS[_ref]['val'], blk, count=1)
    s = s[:st] + blk2 + s[en:]
print('pads re-netted from design.py:', _nr)
report = []; placed = {}
for zname, z in ZONES.items():
    x0, x1, y0, y1 = z['box']; gap = z.get('gap', 0.0)
    cx, top, rowh = x0, y1, 0.0
    for item in z['parts']:
        ref, rot = (item if isinstance(item, list) else [item, 0])
        if ref == '|':          # forced new row
            top -= rowh + gap; cx = x0; rowh = 0; continue
        c = rbox(crt(ref), rot); w, h = c[1] - c[0], c[3] - c[2]
        if cx + w > x1 + 1e-6:
            top -= rowh + gap; cx = x0; rowh = 0
        if top - h < y0 - 1e-6:
            report.append('OVERFLOW %s in %s' % (ref, zname)); continue
        # courtyard lens box: x cx..cx+w, y top-h..top  -> KiCad bbox kx0 = 100+cx, ky0 = 150-top
        ox = 100 + cx - c[0]; oy = 150 - top - c[2]
        move(ref, ox, oy, rot); placed[ref] = zname
        cx += w + gap; rowh = max(rowh, h)
open(PCB, 'w').write(s)
all_refs = set(re.findall(r'\(property "Reference" "([^"]+)"', s))
missing = sorted(all_refs - set(placed) - set(FIXED))
print('\n'.join(report) or 'no overflow'); print('not placed:', missing)
