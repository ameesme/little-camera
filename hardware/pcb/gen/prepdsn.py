# -*- coding: utf-8 -*-
"""Prepare a KiCad-exported DSN for freerouting:
 - inner layers become (type power) so no signal is ever routed on the planes
 - VBUS/VSYS/VBAT/SW_L go into a 0.4 mm Power class
 - define the 0.6/0.3 via padstack the Power class uses
(Without the padstack freerouting 1.9 stalls on a warning dialog.)"""
import sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
for L in ('In1.Cu', 'In2.Cu'):
    a = '(layer %s\n      (type signal)' % L
    assert a in s, L
    s = s.replace(a, '(layer %s\n      (type power)' % L)
i = s.find('(padstack "Via[0-3]_500:250_um"'); d = 0
for j in range(i, len(s)):
    if s[j] == '(': d += 1
    elif s[j] == ')':
        d -= 1
        if d == 0: end = j + 1; break
pad6 = s[i:end].replace('Via[0-3]_500:250_um', 'Via[0-3]_600:300_um').replace(' 500))', ' 600))')
s = s[:end] + '\n    ' + pad6 + s[end:]
s = s.replace('(via "Via[0-3]_500:250_um")', '(via "Via[0-3]_500:250_um" "Via[0-3]_600:300_um")', 1)
PWR = ['VBUS', 'VSYS', 'VBAT', 'SW_L']
i = s.find('(class kicad_default'); j = s.find('(circuit', i)
names = s[i + len('(class kicad_default'):j].split()
end = s.find('\n    )', j) + 6
keep = [n for n in names if n not in PWR]
s = (s[:i] + '(class kicad_default ' + ' '.join(keep) + '\n      ' + s[j:end]
     + '\n    (class Power ' + ' '.join(PWR) + '\n      (circuit\n        (use_via "Via[0-3]_600:300_um")\n      )\n'
     + '      (rule\n        (width 400)\n        (clearance 150)\n      )\n    )' + s[end:])
open(dst, 'w').write(s)
print('prepared', dst)
