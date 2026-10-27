# -*- coding: utf-8 -*-
"""Attach the generated lc.3dshapes STEP models (gen/mk3d.py) to the lc footprints
and their board instances, and add two board-only mechanical footprints so the
3D view / STEP export shows the display panel (with its FPC tail) and the cell."""
import re, sys, os, uuid
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
PCB = os.path.join(ROOT, 'little_camera.kicad_pcb'); LIB = os.path.join(ROOT, 'lc.pretty')
MODELS = ['FPC_BXCONN_FC-05B10P21H20', 'FPC_BXCONN_FC-05B24P21H20', 'USB_C_Kinghelm_KH-TYPE-C-W-SMT-16P',
          'Buzzer_SMD_MLT5020', 'SW_Omron_D2F-A_RightAngle']
MECH = {  # name: (ref, value, x, y, descr, fab half-sizes)
    'MECH_LS027B7DH01': ('MECH1', 'LS027B7DH01 panel', 131.9, 125.41,
        'Sharp LS027B7DH01 glass 62.8 x 42.82 x 1.63 bonded to the back (0.15 mm tape), FPC tail wrapping the bottom edge into J3. 3D/mechanical only.', (31.4, 21.41)),
    'MECH_LP552035': ('MECH2', 'LP552035 cell', 111.6, 126.5,
        'LP552035 LiPo 20 x 35 x 5.5 on ~1 mm foam over the J4 area. 3D/mechanical only.', (10.0, 17.5)),
    'MECH_OV2640_CAM': ('MECH3', 'OV2640 module (mock-up)', 150.5, 125.41,
        'MOCK-UP ONLY: OV2640 module glued on the ESP32 shield, board-centred vertically, lens up; 5 cm FPC loops out of J2 past the top edge and back. 3D/mechanical only.', (4.25, 4.25)),
}
U = lambda: str(uuid.uuid4())
def model_line(n): return '(model "${KIPRJMOD}/lc.3dshapes/%s.step" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))' % n
def close(t, i):
    d = 0
    for j in range(i, len(t)):
        if t[j] == '(': d += 1
        elif t[j] == ')':
            d -= 1
            if d == 0: return j + 1

# 1. mechanical lc footprints
for n, (ref, val, x, y, descr, (hx, hy)) in MECH.items():
    L = ['(footprint "%s"' % n, '\t(version 20240108)\n\t(generator "little-camera-gen")\n\t(generator_version "10.0")\n\t(layer "F.Cu")',
         '\t(descr "%s")' % descr, '\t(tags "mechanical 3d")',
         '\t(property "Reference" "REF**" (at 0 0 0) (layer "F.Fab") (hide yes) (effects (font (size 1 1) (thickness 0.15))))',
         '\t(property "Value" "%s" (at 0 1.5 0) (layer "F.Fab") (hide yes) (effects (font (size 1 1) (thickness 0.15))))' % val,
         '\t(attr board_only exclude_from_pos_files exclude_from_bom)',
         '\t(fp_rect (start %g %g) (end %g %g) (stroke (width 0.1) (type solid)) (fill no) (layer "F.Fab"))' % (-hx, -hy, hx, hy),
         '\t' + model_line(n), '\t(embedded_fonts no)\n)']
    open(os.path.join(LIB, n + '.kicad_mod'), 'w').write('\n'.join(L) + '\n')

# 2. model lines in the lc library files
for n in MODELS:
    f = os.path.join(LIB, n + '.kicad_mod'); t = open(f).read()
    if 'lc.3dshapes' not in t:
        t = re.sub(r'\n\t\(embedded_fonts no\)\n\)\s*$', '\n\t' + model_line(n) + '\n\t(embedded_fonts no)\n)\n', t)
        assert 'lc.3dshapes' in t, n
        open(f, 'w').write(t)

# 3. board instances
s = open(PCB).read(); added = 0
out = []; pos = 0
for m in re.finditer(r'\n\t\(footprint "lc:([^"]+)"', s):
    st = m.start() + 1
    if st < pos: continue
    en = close(s, st); blk = s[st:en]
    if m.group(1) in MODELS and 'lc.3dshapes' not in blk:
        blk = blk[:blk.rstrip().rfind(')')].rstrip() + '\n\t\t' + model_line(m.group(1)) + '\n\t)'
        added += 1
    out.append(s[pos:st]); out.append(blk); pos = en
out.append(s[pos:]); s = ''.join(out)

# 4. mechanical footprints on the board
for n, (ref, val, x, y, descr, _) in MECH.items():
    if '"Reference" "%s"' % ref in s: continue
    mod = open(os.path.join(LIB, n + '.kicad_mod')).read().strip()
    mod = re.sub(r'\n\t\((version|generator|generator_version) [^\n]*\)', '', mod)
    mod = mod.replace('(footprint "%s"' % n, '(footprint "lc:%s"' % n, 1)
    mod = mod.replace('(property "Reference" "REF**"', '(property "Reference" "%s"' % ref, 1)
    lines = mod.split('\n'); blk = '\n'.join(['\t' + lines[0]] + ['\t' + l for l in lines[1:]])
    blk = blk.replace('\n\t\t(layer "F.Cu")', '\n\t\t(layer "F.Cu")\n\t\t(uuid "%s")\n\t\t(at %g %g)' % (U(), x, y), 1)
    i = s.find('\n\t(footprint '); s = s[:i] + '\n' + blk + s[i:]
open(PCB, 'w').write(s)
print('models attached to', added, 'board footprints; mechanical footprints:', ', '.join(v[0] for v in MECH.values()))
