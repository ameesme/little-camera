# point the MECH footprints at the v3 models (display fold for the 0.8 mm board, 403030 cell, sunk camera)
import sys, os
p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'little_camera.kicad_pcb')
s = open(p).read(); n = 0
for a, b in (('MECH_LS027B7DH01.step', 'MECH_LS027B7DH01_v3q.step'), ('MECH_LS027B7DH01_v3.step', 'MECH_LS027B7DH01_v3q.step'), ('MECH_LP552035.step', 'MECH_LP403030.step'),
             ('MECH_OV2640_CAM.step', 'MECH_OV2640_CAM_v3q.step'), ('MECH_OV2640_CAM_v3m.step', 'MECH_OV2640_CAM_v3q.step')):
    n += s.count(a); s = s.replace(a, b)
s = s.replace('"LP552035', '"LP403030')
open(p, 'w').write(s); print('mech models patched:', n)
