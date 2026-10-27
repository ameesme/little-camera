#!/usr/bin/env python3
"""Produce JLCPCB-format BOM and CPL from the board + design table."""
import re, sys, os, csv, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import PARTS
from lcsc import lookup

PCB = sys.argv[1]; OUT = sys.argv[2]
os.makedirs(OUT, exist_ok=True)
s = open(PCB).read()

def close(t, i):
    d = 0
    for j in range(i, len(t)):
        if t[j] == '(': d += 1
        elif t[j] == ')':
            d -= 1
            if d == 0: return j + 1

# parts that JLC must NOT try to place
SKIP_PREFIX = ('MH', 'MECH')
SKIP_REF = {'J4', 'J5', 'SW2', 'SW3'}   # bare test/battery/tweezer pads, no part
HAND_SOLDER = {'SW1'}           # D2MQ-1-TR (v3): through-hole, 20 at LCSC, hand-fit by the user

rows = []
dnp = []
handfit = []
for m in re.finditer(r'\n\t\(footprint "', s):
    i = m.start() + 1
    blk = s[i:close(s, i)]
    ref = re.search(r'\(property "Reference" "([^"]+)"', blk).group(1)
    if ref.startswith(SKIP_PREFIX) or ref in SKIP_REF: continue
    if ref in HAND_SOLDER:
        handfit.append(ref); continue
    if re.search(r'\(property "Value" "DNP"', blk):
        dnp.append(ref); continue
    at = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', blk)
    x, y, rot = float(at.group(1)), float(at.group(2)), float(at.group(3) or 0)
    layer = re.search(r'\n\t\t\(layer "([^"]+)"\)', blk).group(1)
    val = re.search(r'\(property "Value" "([^"]*)"', blk).group(1)
    fp = re.search(r'\(footprint "([^"]+)"', blk).group(1)
    rows.append(dict(ref=ref, val=val, fp=fp, x=x, y=y, rot=rot % 360,
                     layer='top' if layer == 'F.Cu' else 'bottom'))

# board origin -> JLC wants absolute mm with Y flipped relative to KiCad's
# top-left origin; KiCad's own "drill/place file origin" is 0,0 here, so emit
# coordinates relative to the board's lower-left corner.
xs = [float(a) for a, b in re.findall(r'\(gr_line[\s\S]{0,300}?\(start ([-\d.]+) ([-\d.]+)\)', s)]
X0, Y1 = 98.5, 149.2            # v3 board lower-left corner = drill/place origin

# JLC places each part by ITS OWN footprint origin. Where our footprint's origin
# differs from JLC's model, shift the CPL point by (local dx, dy) in footprint
# coordinates.  U1: our ESP32-S3-WROOM-1 origin is the module body centre; JLC's
# (C2913202, WIRELM-SMD_ESP32-S3-WROOM-1) is the centre of the pad field, 3.785 mm
# toward the pin end (pad 1 at y=-5.26 here vs y=-9.045 in JLC's model).
CENTROID = {'U1': (0.0, 3.785),
            # J2: JLC's FC-05B24P21H20 origin sits 3.6 mm from ours (footprint -y, i.e. toward the contact row)
            'J2': (0.0, -3.6)}
# JLC places each part in ITS library orientation. Offsets found by matching our footprint pads onto the
# JLC/EasyEDA footprint of the same LCSC part (pad numbers = datasheet pins): CPL rotation = KiCad rotation + offset.
ROT_OFFSET = {'U2': 180, 'U7': 180, 'U8': 180, 'U10': 270, 'Q1': 180, 'Q2': 180, 'J2': 180}
for r in rows:
    if r['ref'] in CENTROID:
        lx, ly = CENTROID[r['ref']]; a = math.radians(r['rot'])
        r['x'] += lx * math.cos(a) + ly * math.sin(a)
        r['y'] += -lx * math.sin(a) + ly * math.cos(a)
for r in rows:
    r['rot'] = (r['rot'] + ROT_OFFSET.get(r['ref'], 0)) % 360

with open(os.path.join(OUT, 'little_camera-cpl-jlc.csv'), 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
    for r in sorted(rows, key=lambda r: r['ref']):
        w.writerow([r['ref'], f"{r['x']-X0:.4f}mm", f"{Y1-r['y']:.4f}mm",
                    r['layer'].capitalize(), f"{r['rot']:.1f}"])

# BOM grouped by LCSC part (JLC flags two lines that resolve to the same part
# as "Unconfirmed" - that happened with SW2/SW3 on the v1 order).
def short_fp(fp):
    n = fp.split(':')[-1]
    m = re.search(r'_(0402|0603|0805)_', n)
    return m.group(1) if m else n

bylc = {}
for r in rows:
    hit = lookup(r['val'], r['fp'])
    k = hit[0] if hit else ('?', r['val'], r['fp'])
    g = bylc.setdefault(k, dict(vals=[], fp=r['fp'], refs=[], hit=hit))
    g['refs'].append(r['ref']); g['vals'].append(r['val'])

def rsort(refs):
    return sorted(refs, key=lambda r: (re.sub(r'\d','',r), int(re.sub(r'\D','',r) or 0)))

missing = []
extended = 0; standard_only = []
lines = sorted(bylc.values(), key=lambda g: (short_fp(g['fp']), g['vals'][0]))
with open(os.path.join(OUT, 'little_camera-bom-jlc.csv'), 'w', newline='') as f, \
     open(os.path.join(OUT, 'little_camera-bom-annotated.csv'), 'w', newline='') as fa:
    w = csv.writer(f); wa = csv.writer(fa)
    # JLC upload file: exactly the four columns their BOM tool expects
    w.writerow(['Comment', 'Designator', 'Footprint', 'JLCPCB Part #'])
    wa.writerow(['Comment', 'Designator', 'Footprint', 'JLCPCB Part #', 'Qty',
                 'MPN', 'Library', 'Stock', 'Note'])
    for g in lines:
        vals = list(dict.fromkeys(g['vals']))
        comment = vals[0] if len(vals) == 1 else '/'.join(vals)
        refs = rsort(g['refs']); hit = g['hit']
        if hit is None:
            missing.append((comment, g['fp'])); lc = mpn = lib = stock = ''; note = 'UNRESOLVED'
        else:
            lc, mpn, lib, stock, note = hit
            if lib == 'extended': extended += 1
            if 'STANDARD-ONLY' in note: standard_only.append(lc)
        w.writerow([comment, ','.join(refs), short_fp(g['fp']), lc])
        wa.writerow([comment, ','.join(refs), g['fp'], lc, len(refs), mpn, lib, stock, note])
groups = bylc
print('BOM lines:', len(groups), ' extended:', extended, ' basic:', len(groups) - extended - len(missing))
print('standard-only parts:', standard_only)
print('UNRESOLVED:', missing if missing else 'none')
print('placements:', len(rows), ' bom lines:', len(groups))
print('excluded (DNP):', dnp)
print('excluded (hand-fit, through-hole):', handfit)
print('excluded (no part):', sorted(SKIP_REF), '+ mounting holes')
print('wrote', OUT)
